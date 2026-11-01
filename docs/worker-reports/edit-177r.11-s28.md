# edit-177r.11 — worker report

Status: done. Windows and Linux acceptance commands exit 0; live-only execution remains reserved for the coordinator. All runs use `taskset -c 0-15 nice -n 10`, builds use `-j16`. Measurements are (M) [AC, desktop Spiral], while Tobias uses the machine. Initial `uptime`: `10:22:10 up 1:56, 1 user, load average: 0.81, 3.37, 2.73`. During verification: `10:32:30 up 2:07, 1 user, load average: 7.64, 6.74, 4.38`.

## Changes

- `tests/win/test_isolation.h`: shared native-name classifier, unknown fails closed. Used by platform, D3D native/white-box and quiet suites. Quiet tests verify actual requested mode, negative name fixtures, and false environment values.
- `win32_plat_test`: all clipboard cases and foreign-owner selector require a private station. Layout and expose cases use the private desktop; native input remains live-only. Clipboard retry stays covered on station.
- `d3d11_test`: WARP native offscreen resources and exact pixel fixtures remain automated; displayed Present/T5/T6/statistics have explicit SKIPs and stay in the live path. Both isolated modes exercise adapter suspension/occlusion without a rebuild loop. Original live assertions are retained.
- `d3d11_win_test`: expired-occlusion regression.
- `mk/win.mk`, `tools/win_check.py`: bounded suite groups, station default, guarded platform/quiet and native-occlusion desktop passes, grouped release/sanitizer execution, per-pass logs/times, ASan negative control, final SKIP census. `check-live` requires `WIN_LIVE_OK=1` and only selects live cases.
- `tools/win_reparse_fixture.sh`: every command uses the private-station runner.
- `Makefile`: exclude the Windows-only quiet launcher from the Linux tool wildcard; otherwise `make all` fails at `windows.h`.
- Minimal production fixes: `src/win32/win32_clip_win.c` uses the documented default when a station returns zero double-click tolerance; `src/d3d11/d3d11_win.c` retains occluded frames instead of requesting repeated healthy-device rebuilds. Details: `docs/decisions/edit-177r.11.md`.

## Red / green

Commands below have the CPU/nice prefix above. The initial sandbox rejects WSL interop (`UtilBindVsockAnyPort: socket failed 1`); authorized isolated Windows execution uses the outer sandbox escalation. Linux private Xvfb similarly needs socket access. These are execution-environment failures, separately from assertion failures.

- Baseline build: `make TARGET=win -j16 build/win/rel/tests/{win32_plat,d3d11,d3d11_win,quiet_run}_test.exe build/win/tools/win_quiet_run.exe`, exit 0.
- Red: `WINRUN_ISOLATION=station tools/winrun.sh build/win/rel/tests/win32_plat_test.exe`, exit 1, `FAIL (10 failures)` (M: count printed by suite). Layout load at original lines 231 and 594, their AltGr consequences, expose at 320, retry at 546. Log `/tmp/edit-177r.11-red-plat.log`.
- Red: station `d3d11_test.exe`, exit 1, `present timeout frame=1 rc=-8 status=00000000 visible=0 iconic=0`, `device removal=00000000`, original `:318 FAIL first_frame == 0`. Same result with `--warp`. Logs `/tmp/edit-177r.11-red-d3d{,-warp}.log`.
- Red: station `win32_plat_test.exe clip-contention`, exit 1, `retry native double-click ms=0`, failed result bytes. This is a real production retry bug, not a reason to exclude the case. Log `/tmp/edit-177r.11-red-retry.log`.
- Red, new test before fix: station `d3d11_win_test.exe`, exit 1, `:195 FAIL d3d11_present(&b,41) == RENDER_ERR_BUSY`. The fixture starts with an expired deadline and native `DXGI_STATUS_OCCLUDED`. Log `/tmp/edit-177r.11-red-occlusion.log`.
- Green focused: platform station and private desktop both exit 0 with `PASS (0 failures)`; retry executes on station and consumes all injected failures. Native D3D both modes exit 0: `WARP offscreen pixels=exact frames=15 resources/resize/shutdown PASS` (M: frame count printed by each pass). White-box occlusion exits 0. Logs `/tmp/edit-177r.11-green-plat-{station,desktop}.log`, `/tmp/edit-177r.11-green2-d3d-{station,desktop}.log`, `/tmp/edit-177r.11-green-occlusion.log`.
- Reparse create/remove after routing through isolation: both exit 0. Path retained in `/tmp/edit-177r.11-fixture-path.txt`; fixture removed.
- Linux red build before tool-list fix: `make -j16 all`, exit 2, `tools/win_quiet_run.c:15:10: fatal error: windows.h: No such file or directory`; green build after excluding the Windows-only tool exits 0.

## Station failure variability investigation

After the green acceptance run, rebuild the frozen baseline test plus its baseline platform/clipboard implementations under `build/isolation-red/` (read from `git show HEAD:...`, leaving tracked sources untouched). Build command: `taskset -c 0-15 nice -n 10 make -f Makefile -f build/isolation-red/baseline.mk TARGET=win -j16 build/isolation-red/win32_plat_test.exe`, exit 0. Run it only with `WINRUN_ISOLATION=station tools/winrun.sh build/isolation-red/win32_plat_test.exe` and the CPU/nice prefix.

All three repeats exit 1 with the same ten failing assertion lines: 231, 320, 594, 598, 600, 601, 605, 608, 612, 546 (M: `/tmp/edit-177r.11-baseline-repeat-{1,2,3}.log`). Including the first baseline, that is four red runs with the same failure set (P: initial run + three repeats). No timing-dependent assertion failure was demonstrated in this sample. The earlier five-to-ten spread was not reproduced. Layout-load failure and hidden-window expose are station capabilities; retry is the deterministic zero-tolerance bug. The remaining AltGr failures follow a failed layout setup, so their individual outcomes cannot diagnose a timing race. The new retry does not exclude or relax the original result assertion; all injected native-open failures must be consumed. The existing short pump waits in other cases were retained, and both full checks passed them.

## Verification and measured concurrency

All command exit codes below are measured (M) from the command result. Each command has `taskset -c 0-15 nice -n 10`; make commands use `-j16`.

| Command | Exit | Evidence |
| --- | --- | --- |
| `make TARGET=win -j16 check` (initial worker count) | 0 | 100 binaries + 6 desktop passes + ASan negative control; jobs=4, suite phase 495.624 s |
| `make TARGET=win -j16 check` (final default) | 0 | 100 binaries + 6 desktop passes + ASan negative control; jobs=8, suite phase 399.398 s, failures=0 |
| `make TARGET=win -j16 all` | 0 | All release binaries and quiet launcher built |
| `make -j16 all` | 0 | Final run: `make: Nothing to be done for 'all'.` |
| `make -j16 check` | 0 | 117 test binaries, 121 tasks, 16 jobs, 6 private displays; 158.4 s, with dataloss flake described below |
| `make TARGET=win -j16 -n check-live` | 0 | Build prerequisites current; plan below refuses without approval variable |
| `make TARGET=win -j16 -n check-live WIN_LIVE_OK=1` | 0 | Same live selectors, approval-variable check true; never executed |
| `python3 -m py_compile tools/win_check.py`, `bash -n tools/win_reparse_fixture.sh`, `git diff --check` | 0 | Syntax / whitespace checks |

The default `WIN_CHECK_JOBS=8` is bounded independently from compilation `-j16`. Groups keep each suite's release/sanitizer/desktop work serial, so clipboard state cannot collide between this check's platform passes. The raw suite-phase timings above are observational, not a controlled speedup experiment: the initial four-worker run overlapped Linux compilation/checks; the final run used eight workers after Linux completed. Both used the same CPU affinity. No battery exists under `/sys/class/power_supply`; all measurements use the binding [AC, desktop Spiral] stamp. End-of-check `uptime`: `10:45:45 up 2:20, 1 user, load average: 5.83, 7.53, 6.33`; Tobias was using the machine.

Final suite-phase census: 150 printed SKIP occurrences (M: parsed from suite logs plus the preserved journal coverage footer). Clipboard=16 (P: eight groups × two desktop configurations), layouts/expose=8 (P: four groups × two station configurations). Live-only reasons: displayed frame completion=60 (P: fifteen pixel fixtures × two modes × two build configurations); committed presentation=4; diagnostic Present=4; native foreground input=4; Unicode foreground input=4; hardware presentation=4; visible minimize/restore=4 (P: each one case × two modes × two configurations). The remaining 42 occurrences are pre-existing declared coverage gaps/allocator ownership/optional corpus fixtures (P: 150 − 16 − 8 − 84). The full exact per-reason line is preserved in the output tail below; counts are case-group occurrences, not a count of individual assertions.

The native occlusion lines are `status=00000000 visible=0` on station and `status=087a0001 visible=1` on private desktop, for both builds (M: `build/win/check-logs/{rel,san}.d3d11_test.{station,desktop}.log`). Each offscreen run reports exact pixels across its fixtures, including resize and same-HWND replacement. `san.smoke_test.station.negative` exits 1 with an AddressSanitizer report and is counted as PASS, as required. Expected `find fixture FAIL` diagnostics come from the supervisor's intentionally suppressed submissions; both containing suites exit 0.

Final full-output tail (`/tmp/edit-177r.11-win-check-final.log`):

```text
RESULT rel.journal_kill_test.station exit=0 PASS seconds=148.332 log=build/win/check-logs/rel.journal_kill_test.station.log
== san.journal_kill_test.station
journal_hot_exit_test: cache/durable/group/PAINT/rotation/copy process-death recovery passed
journal_kill_test: ok trials=50 script_edits=100000 issued=2661986 page_cache=2661953 replayed=2661975 acknowledged=2610739 (page_cache<=replayed<=issued; piece_tree==independent_byte_model_at_replayed; trial0=kill_before_pump)
RESULT san.journal_kill_test.station exit=0 PASS seconds=234.968 log=build/win/check-logs/san.journal_kill_test.station.log
Windows journal: SKIP editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)
Windows journal: SKIP src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test
Windows check: binaries=100 desktop-passes=6 jobs=8 seconds=399.398 failures=0; SKIP cases=150: AddressSanitizer owns malloc=1; Linux SIGBUS signal-action chaining and real truncation: native Windows faults use the OS fault trigger; NTFS refuses mapped truncation=2; Linux syscall interposition; real-file integration awaits W2.1/W2.2=2; POSIX FIFO, mode/uid/xattr/ACL and syscall-interposition cases: Windows metadata and native errors exercised above=2; clipboard: needs private station=16; editor-dependent group/retire/session/recovery/dataloss integration and first-paint closed-tab restart (editor/platform not ported)=1; hard-link/symlink fixtures: frozen os.h has no link-creation API=2; hl_worker faulted_snapshot (requires W2 mapped file/fault integration)=2; installed layouts are UK/US/German=4; journal/findui integration and editor-dependent cases: those modules await their Windows beads=2; layouts: needs a station with input=8; lineidx combined_ownership (file-backed integration requires W2 file port)=2; name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-ad8e5d3732e178ca (quick: optional all-a fixture absent; full mode requires it)=1; name=G6_dense_a file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-f9740d62abbd2cb3 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6c file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-ad8e5d3732e178ca (quick: optional all-a fixture absent; full mode requires it)=1; name=G6c file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-f9740d62abbd2cb3 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-ad8e5d3732e178ca (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_a31b file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-f9740d62abbd2cb3 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-ad8e5d3732e178ca (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_early file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-f9740d62abbd2cb3 (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-ad8e5d3732e178ca (quick: optional all-a fixture absent; full mode requires it)=1; name=G6v_first_last_middle file=C:\Users\tobia\AppData\Local\Temp/sublimite-test-find-bench-f9740d62abbd2cb3 (quick: optional all-a fixture absent; full mode requires it)=1; needs live desktop: committed presentation=4; needs live desktop: diagnostic Present validates timing=4; needs live desktop: displayed frame completion=60; needs live desktop: foreground Unicode input=4; needs live desktop: foreground native input=4; needs live desktop: hardware presentation=4; needs live desktop: visible minimize and restore=4; requires Windows main/platform (W2.4)=2; sanitizer owns allocator=2; sanitizer owns malloc=1; src/file save integration (file module not ported); journal_save_prepare/finish/cancel run in journal_safety_test=1; trace_pairs Python cases (SUBLIMITE_TEST_PYTHON native interpreter path unset)=2; work_foreground editor jump (requires Windows editor/platform port)=2; work_foreground real clients (8 cases require W2 savectl/findui/font discovery)=2
```

`check-live` plan, printed only:

```sh
test "" = 1 || { echo "check-live refuses: set WIN_LIVE_OK=1 only with Tobias's explicit approval" >&2; exit 2; }
set -e; for t in build/win/rel/tests/win32_plat_test.exe build/win/rel/tests/d3d11_test.exe build/win/san/tests/win32_plat_test.exe build/win/san/tests/d3d11_test.exe; do \
  echo "== $t --live-only (live desktop)"; \
  WINRUN_ISOLATION=none tools/winrun.sh $t --live-only; \
  done
```

The corresponding plan with `WIN_LIVE_OK=1` changes the first line to `test "1" = 1`; neither plan was executed. The live selectors have no clipboard entry.


## Orphans / desktop safety

The hold fixture prints its own Windows PID and waits on the private station. Python starts `tools/winrun.sh ... quiet_run_test.exe --hold`, sends SIGTERM only to its own WSL child, waits, then invokes isolated `tasklist.exe /fo csv /nh`. WSL exit -15; held Windows PID 68816 absent and no `*_test.exe` remained (M: tasklist filter). The only `win_quiet_run.exe` in the listing was the active tasklist runner. Log `/tmp/edit-177r.11-orphan-tasklist.log`. The descendant variant starts parent PID 69976 and child PID 52932, then SIGKILLs only its WSL relay. WSL exit -9; both recorded PIDs absent from the subsequent tasklist (M). Logs `/tmp/edit-177r.11-orphan-tree-{hold,tasklist}.log`. Final tasklist after the complete Windows check exits 0 and contains no `*_test.exe`; `/tmp/edit-177r.11-final-tasklist.log`. A later broad global assertion after baseline repeats returned 1 because other work had started `editor_win_test.exe` PID 46828 and `file_win_test.exe` PIDs 64644/69244 after that clean census. Our baseline executable and all recorded hold PIDs were absent; those unrelated processes were not terminated. Log `/tmp/edit-177r.11-post-baseline-tasklist.log`. No live run, input injection, desktop switch, foreground activation API, clipboard write in WinSta0, process-name kill, or user registry/profile write was performed.

## Found, not fixed

Linux `make check` exits 0, with the existing runner reporting `FLAKY dataloss_test`: `:940 FAIL trace_now_ns() < limit`, `:2071 FAIL detached_rewrite(false) == 0` under parallel load, then passing alone. No unrelated source fix was made. The run printed `117 test binaries passed in 158.4 s (parallel, 16 jobs, 6 displays)` (M: runner output).

The existing Windows journal footer says file integration is unported even though `WIN_MODULES` contains file/journal. Preserve its declared coverage gap, including in the final skip census, for the coordinator to reconcile separately. Existing IME-engine, link-creation API, and syscall-interposition gaps remain explicit SKIPs.

## Open questions

Only the coordinator may run `check-live` after Tobias's explicit approval. It has not been run here.

## Assertion preservation / guarded-case inventory

Original-source line numbers below refer to baseline `HEAD`, so the mapping survives inserted helper lines. No CHECK/T assertion was removed. Existing clipboard-expiry pumping changes from the unavailable station preference to the same effective fallback tolerance; its assertion still runs on station. Native offscreen code reuses the original pixel fixtures and separately asserts exact pixels, completed GPU queries, caller-data snapshotting and zero allocations. It never forges adapter completion.

### win32_plat_test: keyboard — private desktop

```text
231: CHECK(us!=NULL)
231: CHECK(ActivateKeyboardLayout(us,0)!=NULL)
235: CHECK(e && e->press && e->keysym==XKB_KEY_a && e->utf8_len==1 && e->utf8[0]=='a')
236: CHECK(kind(f,PLAT_EV_KEY)==n)
237: CHECK(e && !e->press && !e->utf8_len)
239: CHECK(e && e->keysym==XKB_KEY_A && e->mods==(PLAT_MOD_SHIFT|PLAT_MOD_CTRL) && !e->utf8_len)
241: CHECK(e && e->keysym==XKB_KEY_Return && !e->utf8_len)
244: CHECK(kind(f,PLAT_EV_KEY)==n)
246: CHECK(e && e->keysym==0x0101f642u && e->utf8_len==4)
247: CHECK(SendMessageW(window(f),WM_UNICHAR,UNICODE_NOCHAR,0)==TRUE)
250: CHECK(e && e->keysym==0x0101f642u && e->utf8_len==4)
251: CHECK(SetKeyboardState(numkeys))
253: CHECK(e && e->keysym==XKB_KEY_KP_1 && e->utf8_len==1 && e->utf8[0]=='1')
263: CHECK(ActivateKeyboardLayout(german,0)!=NULL)
265: CHECK(e && e->keysym==XKB_KEY_at && e->utf8_len==1 && e->utf8[0]=='@')
266: CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)))
269: CHECK(acute!=-1)
272: CHECK(kind(f,PLAT_EV_KEY)==n)
274: CHECK(e && e->keysym==XKB_KEY_eacute && e->utf8_len==2 && e->utf8[0]==0xc3 && e->utf8[1]==0xa9)
275: CHECK(pump(f,25)==PLAT_OK)
275: CHECK(kind(f,PLAT_EV_KEY)==n)
281: CHECK(kind(f,PLAT_EV_KEY)==n)
284: CHECK(ActivateKeyboardLayout(original,0)!=NULL)
```

### win32_plat_test: altgr_test — private desktop

```text
594: CHECK(de!=NULL)
595: CHECK(ActivateKeyboardLayout(de,0)!=NULL)
598: CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)))
600: CHECK(e && (e->mods&PLAT_MOD_ALTGR) && !(e->mods&(PLAT_MOD_CTRL|PLAT_MOD_ALT)))
601: CHECK(e && (e->mods&PLAT_MOD_ALTGR))
603: CHECK(e && e->mods==(PLAT_MOD_CTRL|PLAT_MOD_ALT) && !e->utf8_len)
605: CHECK(e && (e->mods&(PLAT_MOD_ALTGR|PLAT_MOD_CTRL))==(PLAT_MOD_ALTGR|PLAT_MOD_CTRL) && !e->utf8_len)
608: CHECK(e && (e->mods&(PLAT_MOD_ALTGR|PLAT_MOD_CTRL))==(PLAT_MOD_ALTGR|PLAT_MOD_CTRL) && !e->utf8_len)
610: CHECK(ActivateKeyboardLayout(us,0)!=NULL)
612: CHECK(e && e->mods==PLAT_MOD_ALT && !e->utf8_len)
613: CHECK(ActivateKeyboardLayout(saved,0)!=NULL)
```

### win32_plat_test: altcode_test — private desktop

```text
688: CHECK(ActivateKeyboardLayout(us,0)!=NULL)
699: CHECK(letters==1 && e && e->utf8_len==1 && e->utf8[0]=='A')
700: CHECK(ActivateKeyboardLayout(saved,0)!=NULL)
```

### win32_plat_test: expose — private desktop

```text
320: CHECK(InvalidateRect(window(f),NULL,FALSE))
320: CHECK(pump(f,0)==PLAT_OK)
320: CHECK(kind(f,PLAT_EV_EXPOSE)>0)
```

### win32_plat_test: Unicode clipboard / PRIMARY — private station

```text
326: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,text,sizeof text-1)==PLAT_OK)
327: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
327: CHECK(pump(f,100)==PLAT_OK)
330: CHECK(e && e->clip_ok && len==sizeof text-1 && bytes && !memcmp(bytes,text,len))
331: CHECK(plat_clip_set(&f->p,PLAT_CLIP_PRIMARY,"ignored",7)==PLAT_OK)
332: CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK)
332: CHECK(pump(f,100)==PLAT_OK)
333: CHECK(e && e->clip_which==PLAT_CLIP_PRIMARY && !e->clip_ok)
334: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"\xff",1)==PLAT_ERR_FAIL)
```

### win32_plat_test: clipboard_async_test — private station

```text
536: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"async",5)==PLAT_OK)
537: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
537: CHECK(pump(f,100)==PLAT_OK)
538: CHECK(clip_ui_reads==0)
538: CHECK(f->clip_results==1 && f->clip_sizes[0]==5)
```

### win32_plat_test: clipboard_contention_test — private station

```text
544: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"retry",5)==PLAT_OK)
545: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
545: CHECK(pump(f,700)==PLAT_OK)
546: CHECK(f->clip_results==1 && f->clip_sizes[0]==5 && !memcmp(f->clip_bytes[0],"retry",5))
```

### win32_plat_test: clipboard_results_test — private station

```text
552: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"first",5)==PLAT_OK)
553: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
554: CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK)
555: CHECK(pump(f,150)==PLAT_OK)
556: CHECK(f->clip_results==3 && f->clip_sizes[0]==5 && !memcmp(f->clip_bytes[0],"first",5))
557: CHECK(f->clip_sizes[1]==0 && f->clip_sizes[2]==0)
```

### win32_plat_test: clipboard_newlines_test — private station

```text
564: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,input,sizeof input-1)==PLAT_OK)
565: CHECK(pump(f,100)==PLAT_OK)
565: CHECK(OpenClipboard(window(f)))
567: CHECK(wide && !wcscmp(wide,L"a\r\nb\r\nc\r\n\xd83d\xde42"))
569: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
569: CHECK(pump(f,100)==PLAT_OK)
572: CHECK(bytes && len==sizeof expected-1 && !memcmp(bytes,expected,len))
```

### win32_plat_test: clipboard_nul_test — private station

```text
578: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"keep",4)==PLAT_OK)
579: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"a\0b",3)==PLAT_ERR_FAIL)
580: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
580: CHECK(pump(f,100)==PLAT_OK)
582: CHECK(bytes && len==4 && !memcmp(bytes,"keep",4))
```

### win32_plat_test: clipboard_foreign_test — private station

```text
481: CHECK(os_thread_start(&guard,foreign_guard,&c)==OS_OK)
483: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
484: CHECK(pump(f,250)==PLAT_OK)
485: CHECK(os_thread_join(&guard)==OS_OK)
485: CHECK(!c.rescued && clip_ui_reads==0)
486: CHECK(f->work==1 && f->clip_results==1 && f->clip_sizes[0]==1024u*1024u)
```

### win32_plat_test: clipboard_foreign_contention_test — private station

```text
493: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"foreign",7)==PLAT_OK)
494: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
495: CHECK(pump(f,150)==PLAT_OK)
496: CHECK(f->clip_results==1 && f->clip_sizes[0]==7 && !memcmp(f->clip_bytes[0],"foreign",7))
500: CHECK(plat_clip_request(&f->p,PLAT_CLIP_CLIPBOARD)==PLAT_OK)
501: CHECK(pump(f,(int)GetDoubleClickTime()+200)==PLAT_OK)
502: CHECK(f->clip_results==1)
502: CHECK(e && e->code==1 && !e->clip_ok)
503: CHECK(plat_clip_set(&f->p,PLAT_CLIP_CLIPBOARD,"pending",7)==PLAT_OK)
```

### win32_plat_test: foreign_start — private station

```text
455: CHECK(GetModuleFileNameW(NULL,exe,32768)>0)
459: CHECK(count>0)
460: CHECK(CreateProcessW(exe,command,NULL,NULL,TRUE,0,NULL,NULL,&start,&info))
462: CHECK(WaitForSingleObject(c->ready,3000)==WAIT_OBJECT_0)
```

### win32_plat_test: foreign_stop — private station

```text
467: CHECK(WaitForSingleObject(c->process,3000)==WAIT_OBJECT_0)
468: CHECK(GetExitCodeProcess(c->process,&code) && code==0)
```

### win32_plat_test: clipboard callback lifetime assertions — private station

```text
189: CHECK(plat_clip_request(&f->p,PLAT_CLIP_PRIMARY)==PLAT_OK)
191: CHECK(after==len && again==bytes && (!len || !memcmp(again,bytes,len)))
```

### win32_plat_test: Unicode SendInput — check-live, own foreground window

```text
641: CHECK(SendInput(6,input,sizeof input[0])==6)
641: CHECK(pump(f,30)==PLAT_OK)
646: CHECK(len==6 && !memcmp(bytes,"\xce\xbb\xf0\x9f\x99\x82",6))
```

### win32_plat_test: F12 SendInput — check-live, own foreground window

```text
845: CHECK(SendInput(2,input,sizeof input[0])==2)
845: CHECK(pump(&f,30)==PLAT_OK)
846: CHECK(e && e->keysym==XKB_KEY_F12)
```

### d3d11_test: original frame adapter / presentation assertions — check-live

```text
123: T(render_dirty_strips(grid,strips,8,&count) == RENDER_OK)
128: T(d3d11_notify_latency_ready(b) == RENDER_OK)
131: T(rc == RENDER_OK)
132: T(p->t5_count == n5 && p->t6_count == n6)
133: T(render_backend_submit(b,grid,strips,count) == RENDER_ERR_BUSY)
144: T(IsIconic(window))
145: T(render_backend_present(b,p->id) == RENDER_ERR_BUSY)
146: T(d3d11_poll_completions(b) == RENDER_OK && !d3d11_recovery_required(b))
147: T(p->t5_count == n5 && p->t6_count == n6)
148: T(!IsIconic(window))
156: T(d3d11_present_stats(b,&info) == RENDER_OK)
169: T(rc == RENDER_OK)
170: T(render_backend_present(b,p->id) == RENDER_ERR_STATE)
172: T(d3d11_read_pixels(b,got,136*80,w) == RENDER_OK)
186: T(d3d11_present_stats(b,&info) == RENDER_OK)
200: T(!edit_malloc_guard_active() || allocations == 0)
201: T(p->t5_count == n5+1 && p->t6_count == n6+1 && !p->bad)
202: T(p->t5 >= b->submitted_ns && p->t6 >= p->t5)
203: T(d3d11_present_stats(b,&info) == RENDER_OK)
204: T(b->info.capabilities & RENDER_CAP_PRESENT_TIMING)
205: T(info.expected && info.observed == info.expected && info.sync_qpc && info.statistics_matched)
207: T(d3d11_poll_completions(b) == RENDER_OK)
208: T(p->t5_count == n5+1 && p->t6_count == n6+1)
210: T(render_backend_event(b,&stale) == RENDER_ERR_FRAME)
```

All frame caller assertions still run under isolation, using real WARP snapshot/draw/staging pixels. Dirty-strip generation, native submit, metadata reuse, exact pixel comparisons, completed native query and allocation guard remain checked offscreen. Public adapter submit/duplicate-BUSY checks run in the isolated occlusion case. Original adapter completion, native readback-after-Present, exactly-once hooks, stale completion and latency-notification checks above still run live; each isolated frame prints its live-only SKIP.

### d3d11_test: remaining live-dependent assertions

- Ordinary visible diagnostic-init success and PRESENT_TIMING validation remain in the original live hardware/WARP and background/ordinary combinations. Isolation adopts background mode and prints diagnostic-Present and hardware-presentation SKIPs. The original capability expression `T(((b.info.capabilities & RENDER_CAP_PRESENT_TIMING) != 0) == !background)` is retained, with `background` set to the actual mode.
- Missing-platform rollback, hidden ordinary-init rollback, successful background device/resource init, WARP identity, latency-handle existence, init-only options, resize, invalid-strip rejection, debug messages/live children, hook suppression at shutdown and same-HWND re-init all run under isolation.
- Original line 371: `T(stats.submitted_frames == p.stats_frames && stats.presented_frames == p.stats_frames)` — check-live. Isolated stats assert zero adapter presents; pending shutdown retains a BUSY, uncommitted frame.
- Original line 387: `T(present_rc == RENDER_OK && b.active)` — check-live. Isolated stats assert zero adapter presents; pending shutdown retains a BUSY, uncommitted frame.
- Original line 381: `T(d3d11_present_stats(&b,&pending) == RENDER_OK)` — check-live. Isolated stats assert zero adapter presents; pending shutdown retains a BUSY, uncommitted frame.
- `T(execution)` around `SetThreadExecutionState` is live-only; isolated runs do not request that Tobias's display stay awake.
- The new native and mock-vtable occlusion assertions run under isolation; no original assertion accepts a fabricated timestamp or capability.
