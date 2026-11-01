# Native Windows window operations — session 28

Verdict: done. All required checks exit 0. No live desktop was used. The
coordinator must perform the physical-mouse checklist.

The coordinator's diagnosis held for native move/size/menu cancellation. The
editor used plat_quit to end a pump turn; modal_service interpreted that as
application termination and posted WM_CANCELMODE. Both a platform fixture with
the editor's stopping callback and the real editor reproduced the failure.
Maximize/minimize/restore commands do not enter that loop. Their native command
paths and caption double-click pass; a physical maximize-button failure was not
reproduced, and Tobias's report did not establish that he tried that button.

Changes:

- Windows-only win32_plat_stop_turn separates pump completion from plat_quit.
  Native modality ignores the former and cancels for the latter. WM_CLOSE,
  WM_QUERYENDSESSION and successful WM_ENDSESSION explicitly cancel modality.
- A Windows-only on_modal_turn callback runs an ordinary editor turn at the
  suspended outer pump boundary. win_modal_turn skips a nested plat_run_for;
  servicing/callback_depth guard application callback reentry. Close settlement
  and synchronous renderer lifecycle work wait for native modality to unwind.
- The ordinary editor turn handles input, layout/resize, frames, work results,
  saves, checkpoints, journal scheduling and clipboard expiry during the hold.
  D3D keeps drawing/completion authentication eligible while excluding native
  holds from the missing-frame watchdog; release gets the ordinary grace window.
  Surface parking/backoff and actual device-error handling remain in place.
- No typing-path allocation was added. Linux continues to call plat_quit as
  before; its callback structure and backend behavior are unchanged.
- check-live prints docs/testing/windows-check-live.md. It covers caption drag,
  drag restoration, Aero Snap, caption double-click, all caption buttons, every
  edge/corner, redraw during sizing, long holds, Alt+Space and close/save behavior.

Files: src/win32/{private.h,win32_plat.c}, src/x11/plat.h,
src/editor/{private.h,editor.c}, src/d3d11/d3d11_win.c,
tests/win/{win32_plat_test.c,editor_win_test.c,win32_drop_test.c}, mk/win.mk,
docs/decisions/edit-177r.31.md, docs/testing/windows-check-live.md, this report.
The drop fixture uses designated callback initialization for the extra
Windows-only callback. The design decision includes the reentry/lifecycle audit.

Before the full modal editor turn, platform_work could already drain mailboxes,
adopt saves and run some retention/source maintenance on a work wake. That did
not drain the editor FIFO, finish resize/layout, submit/present fresh content,
schedule the ordinary journal batches/deadlines, or expire clipboard publication
in editor_clip_poll. Continuations could stall after their wake was drained.
The full ordinary turn restores those phases, without a second native dispatcher.

Red/green evidence (M), [AC, desktop Spiral], Tobias using the box. Every build
and test uses taskset -c 32-47 nice -n 10; builds use -j16 (G, brief: the inclusive
CPU range). Every Windows executable uses tools/winrun.sh. The commands below
show the executable/selector after that common prefix.

- Red: tools/winrun.sh build/win/rel/tests/win32_plat_test.exe turn, exit 1:
  `FAIL ... c.survived`; log build/s28-red-plat.log.
- Red: tools/winrun.sh build/win/rel/tests/editor_win_test.exe --modal-turn,
  exit 1: `FAIL rc>=0 && c.survived && c.progressed && c.exited && !c.rescued`;
  log build/s28-red-editor.log.
- Green: those focused runs exited 0; build/s28-green-plat.log and
  build/s28-green-editor.log. The complete final editor reruns below also pass
  the strengthened modal variants.
- Red held-watchdog seam: --modal-clock, exit 1 at
  `!d3d11_recovery_required`; build/s28-red-clock.log. Green: same selector,
  exit 0; build/s28-green--modal-clock.log. It tests elapsed hold, release grace,
  and ordinary expiry after release with no fabricated native completion.
- Editor variants --modal-turn, --modal-close, --modal-session, --modal-save,
  --modal-clip, --modal-draw and --modal-clock pass in both isolated full editor
  suites. They observe loop survival, more than EDITOR_INPUT_CAP native releases
  without FIFO rejection, actual work delivery, resized grid/frame progress,
  journal durability, save completion, adoption of an explicitly owed worker
  checkpoint, and clipboard timeout with the original text kept. WM_CLOSE and
  WM_QUERYENDSESSION reach EDITOR_CLOSED without rescue. Draw instrumentation
  observes real DrawInstanced calls from win_modal_turn after resize (`draw=1`).
  Release assertions and the full typing run use the active allocation guard;
  the sanitizer allocator is explicitly inert, as before.

Test-fixture corrections: close now exits before an old producer's synchronous
rescue send, which could deadlock against UI join; that rescue is posted. Sent
probes yield at the existing test wait cadence. Exit guards reuse the editor
suite's existing semantic hang guard, permitting inactive-desktop redraw RPCs.
A text probe under synthetic Alt was correctly suppressed, so the event probe
uses a key release. Hidden system menus did not reliably consume posted Escape
on either station or desktop; their extra survival test enters SC_KEYMENU and
uses explicit WM_CANCELMODE. The existing case still tests Alt+Space routing.
Plain SC_MOVE and SC_SIZE regressions finish by posted Enter/Escape without
rescue; physical menu dismissal remains in check-live.

Verification (M):

- taskset -c 32-47 nice -n 10 make -j16 all: exit 0;
  build/s28-make-all.log.
- taskset -c 32-47 nice -n 10 env DEBUGINFOD_URLS= make -j16 check: exit 0;
  `117 test binaries passed` (M, runner summary; its fixed editor partition gives
  121 tasks); build/s28-make-check.log.
- First WIN_CHECK_JOBS=4 make -j16 TARGET=win check: exit 2, deliberately
  interrupted after finding the old platform producer/join deadlock and a
  release editor child wait/exit assertion. Log build/s28-win-check.log. The
  failing editor child was next after close-prompt (--guard); the composite
  assertion did not print its native wait result/exit code. It is not attributed
  to the brief's named --close-queue timeout, and its watchdog was not changed.
- Full editor suites rerun alone, release and sanitizer: exits 0/0;
  build/s28-isolated-editor-rel.log and build/s28-isolated-editor-san.log.
  The release suite reports `keys=10000 ... mallocs=0 guard=active` (M: the
  inherited typing fixture size; allocations measured by its counting guard).
- Final `taskset -c 32-47 nice -n 10 env WIN_CHECK_JOBS=4 make -j16 TARGET=win check`: exit 0; build/s28-win-check-final.log.
  Runner summary: 130 release/sanitizer binaries, 6 additional private-desktop
  passes, jobs=4, failures=0 (M: runner counts); 166 explicitly reported SKIPs
  (M: runner reason census, including existing unsupported/live-only cases).
  Win32 platform release/sanitizer passes on station and desktop all exit 0;
  editor release/sanitizer also pass in this final full run.

Initial build load: 0.96 / 0.47 / 0.69 (M, uptime). Concurrent verification load:
5.98 / 4.87 / 3.07 (M, uptime). Both are [AC, desktop Spiral]; Tobias was using
this desktop. These are context samples, not performance budgets.

Found, not fixed: the first full run's existing --guard child wait/exit assertion
passed when its suite was rerun alone. No other module or child watchdog was
changed. The final full Windows run has no failures. No out-of-scope production
issue was fixed.

What the quiet tests do not prove: physical caption/button hit tracking,
drag-to-restore, Aero Snap, real edge/corner motion, physical menu dismissal, or
visible frame completion. Programmatic SetWindowPos/keyboard-mode SC_MOVE and an
offscreen visibility seam prove UI turn/geometry/drawing progress, not displayed
presents. The editor draw/watchdog seams fabricate no displayed completion;
the platform-only fixture uses synthetic mailbox records to test dispatch. The
coordinator must run check-live on
Tobias's explicit word; that is the remaining live validation, not agent desktop
access authorization.
