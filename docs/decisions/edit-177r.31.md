# Native Win32 modality and editor turns

The editor uses a platform pump as one phase of an editor turn. It formerly
called `plat_quit` from event/work callbacks to end that phase. Win32's modal
service interpreted the same flag as application termination and posted
WM_CANCELMODE. A real keyboard-mode SC_MOVE regression with the editor's stop
callback confirms the diagnosis; the real editor fails the same survival test.

Windows now has a private `win32_plat_stop_turn` operation. It stops normal
message draining, but native move/size/menu loops ignore that turn flag and keep
servicing callbacks. `plat_quit` remains termination and cancels native modality.
WM_CLOSE, WM_QUERYENDSESSION and a successful WM_ENDSESSION request explicit
modal cancellation, independent of a turn stop. Their CLOSE event still enters
the editor's existing admitted-input/clipboard/durability close boundary. Linux
continues to use `plat_quit` exactly as before; the extra callback is Windows-only.

A Windows-only on_modal_turn callback runs after each guarded modal service
slice and on its existing native timer. The outer editor_step is suspended at
its pump boundary, with no borrowed layout/renderer data across that boundary.
The callback runs an ordinary editor_step with a per-editor win_modal_turn guard;
its pump polls sources and GPU completions but skips plat_run_for. Win32's
servicing and callback_depth guards prevent callbacks while that turn runs.
Messages caused synchronously by resize/paint append to the fixed platform ring
and are delivered on a following service slice. No nested native pump, new
thread, input-path allocation or special rendering implementation is introduced.
The ordinary turn advances font/layout, input FIFO, work results, save completion,
retention/checkpoint admission, journal durability, clipboard deadlines and
submission/presentation. Existing slice bounds and fixed arenas still apply.
Close settlement and synchronous renderer rebuilds wait until native modality
unwinds; lifecycle workers can synchronously message the UI and need its pump.

DWM may retain latency/statistics while User32 tracks a native operation. The
existing D3D pause-observation code now refreshes the missing-frame deadline
while modal and on its first observation after release. It does not mark the
surface invisible or manufacture T4/T5/T6, and drawing remains eligible. Actual
device errors and explicit suspension retain their existing behavior. A retained
latency seam tests elapsed hold, release grace and normal expiry after release.
The inherited watchdog duration remains unchanged (G: D3D11_COMPLETION_NS).

The maximize/minimize/restore commands do not enter WM_ENTERSIZEMOVE. They and
caption double-click reach DefWindowProc and pass the command-path tests. Native
button tracking and Aero Snap still require a physical mouse. The check-live
checklist in docs/testing/windows-check-live.md is printed by the live target;
quiet tests explicitly skip those claims.

Audit detail: existing platform_work already called editor_poll_sources, so work
mailboxes, save adoption, file/source maintenance and some retention/checkpoint
admission could progress on a native work wake. That alone could not drain the
editor FIFO, complete layout and resize, submit/present a fresh frame, run the
ordinary journal batch/deadline pump, or expire a clipboard publication in
editor_clip_poll. Once a wake was drained, service continuations could also wait
for another wake. The modal application turn restores those ordinary phases;
it preserves the existing unavailable-surface parking and retry/backoff policy.

Fixture corrections: a close can now finish the native loop before an old test's
synchronous rescue send; posting the rescue avoids joining a producer that still
needs UI dispatch. Sent observation probes yield using the suites' existing wait
cadence so native low-priority timers can run. These changes affect tests only.

The additional system-menu survival test uses SC_KEYMENU and explicit
WM_CANCELMODE for dismissal. Both station and private-desktop runs delivered
work/events but did not reliably treat a posted Escape as physical menu input;
those failed dismissal probes are retained in the build logs. Plain SC_MOVE and
SC_SIZE regressions still assert posted Enter/Escape exits without rescue.
Alt+Space routing remains covered by the existing native-menu case. Physical
menu dismissal is explicitly part of the live checklist.
