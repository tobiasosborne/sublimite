# edit-zzj.15 — session 8 completion

The consultant's EGL choice in [e6x.16.md](e6x.16.md) remains settled.
The implementation and original test-first evidence are in
[zzj.15.md](zzj.15.md); this record gives the current rebased outcome.

`src/main.c` has one selection call: unset `EDIT_BACKEND` selects EGL,
with explicit `gl` and `raster` overrides. Its open configuration enables
one raster fallback. Other editor callers opt into that startup policy.
Only a failed GPU backend init invokes it. File/platform/setup errors and
later rendering errors retain their existing error behavior.

Both backend state reservations and the sleeping raster worker pool are
prepared before initialization. Failed GL initialization releases its native
resources; raster receives fresh aligned state. The startup diagnostic includes
the render error code and is emitted once, before the typing loop. The GL API
groups loader/EGL/Present unavailability under `RENDER_ERR_UNSUPPORTED`, so the
message names that group rather than claiming an unavailable substage detail.
There is no fallback retry in the loop and no additional global state.

EGL completion requires platform callback routing and private-event/fence
polling. The WIP integration remains: poll only an active, presented GPU frame,
with a bounded 1 ms continuation (G), then disarm immediately when it completes.
Successful EGL startup leaves the reserved CPU workers asleep; initialization
joins its existing one-shot thread. Native GL correctness fixes and the
inherited raw-pthread/mailbox contract deviation remain separate work.

Session 8 strengthens the fallback regression without adding production
behavior. It selects the default, injects `RENDER_ERR_INIT` directly at the
backend init operation, separately exercises the real loader-failure seam,
checks exact error preservation, types successfully on raster, and captures
diagnostics through typing and close. Disabling fallback must preserve the
original error and leave the backend uninitialized. A duplicate-diagnostic
mutation fails the one-line assertion. Existing release allocation and idle
fixtures remain for both selections; native EGL acceptance requires a display
with usable native EGL/Present integration.

G1/G11 rows remain for null, raster, and EGL selection. Fallback rows are
explicitly labeled `gl_fallback_raster`; `--require-gl` rejects fallback.
Do not treat Xvfb fallback as native EGL evidence. The existing G1 ingress and
viewport-dimension review findings are not amended by this backend-selection
bead, and the raster blinking G11 issue remains separate.

Current verification, red/green transcripts, TRACK results, and remaining
coordinator checks are in
[the mandatory worker report](../worker-reports/edit-zzj.15-s8.md).


Session 8b: raster completion has one UI wake source: its work mailbox. The
editor disables the platform's duplicate Present subscription after successful
raster initialization (including fallback); EGL retains its platform routing.
Poll accounting and exact idle assertions are unchanged. Failed EGL state
storage is reclaimed to its editor-arena mark, after GL init's existing native
rollback. The selected-backend fixture closes its owner even on assertion
failure; the coordinator's leaked raster snapshots came from the failed idle
fixture skipping editor_close. Full display/leaks-on verification remains
pending because this sandbox cannot create/connect to the required :99 server.
Evidence: [session 8b report](../worker-reports/edit-zzj.15-s8b.md).
