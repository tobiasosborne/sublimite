# edit-e6x.28 — completion of Present/XTest preflight and WM close

Continuation of the tool protocol and calibration decisions in
[P2.6](P2.6.md), including its “Real-display attempt” and subsequent WIP
appendices. Worker verification uses Xvfb `:99` exclusively; there is no
hardware-display verdict here.

## Keyboard precondition

Mapping and XTest availability do not establish that a sample will produce a
fresh press. The new failing fixture holds the sample key through XTest while
running both dry-run entry points. The injector previously approved that
display. Preflight now queries the server key bitmap and refuses a held key,
including modifiers that would change the editor's translation. It names the
held keycode and never releases keys to repair the condition. Injection repeats
this check before each sample. The check is tool protocol IO, outside the
editor's allocation-guarded mutation/layout/submit path.

A paired dry run (`--editor-window`) also implies that its primary target is
the reference window. It must validate the reference READY version/keycode
property even if the invocation omits `--wait-reference`; previously that dry
run could approve an arbitrary mapped window before the real injector rejected
it. The new regression first observed that false success, then passed after
making paired preflight require the marker. The standalone editor-only probe
still accepts an ordinary mapped target.

## Urgent close and persistence

Keep the WIP's minimal loop fix: CLOSE latches the existing per-editor quit
flag outside the bounded command queue. The loop returns CLOSED immediately
after the platform pump observes close, before view/layout continuation or
another submit. A delivered close is handled in one editor turn (G structural
test), even with full input storage, a busy view, and an unfinished Present.

The CLI already calls `editor_flush` before `editor_close`; this remains the
durability boundary. The regression stages an applied edit behind a held
frame, sends native WM_DELETE_WINDOW on `:99`, flushes without completing that
frame, checks the durable journal cutoff, and independently replays the edit.
No platform or journal implementation changes are needed for this case.

Urgent stop preserves applied edits, not commands still waiting in the editor
queue. This is an explicit limitation of the minimal fix, now documented in
the public editor header. The coordinator must reconcile it with
`edit-zzj.13` and [editor review finding 1](../reviews/editor-1.md), which asks
for pending-input preservation. Do not present this bead as resolving that
larger input-order/recovery requirement. Synchronous journal IO and worker
quiescence can take longer than a physical frame on a loaded machine; a
hardware one-frame exit deadline remains unmeasured. No timing was used as a
pass/fail gate.

## Test state and scope

The scripted clock fixture now owns all its state on the stack. Its
compile-time NotifyMSC seam uses a real checked no-op request for the cookie;
window, focus, extension, and connection checks still use the live X server.
The script exercises stable panel-rate streams, zero-clock bootstrap, rate
change, and invalid clock domains. These are synthetic fixtures, not measured
panel rates. The new `refproto_fuzz` covers numeric, wire and clock arithmetic;
live preflight remains an integration-test responsibility.

No new globals, backend policy changes, compositor changes, or unrelated
review fixes. The unrelated sanitizer shutdown failure observed during
`file_kill_test` is recorded in the worker report without a code change.

Red/green and final build/test/fuzz evidence are in
[the mandatory session report](../worker-reports/edit-e6x.28-s8.md).
