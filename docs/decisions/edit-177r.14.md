# edit-177r.14: reusable Windows editor fixture

## Two backends, because the null backend has no window
The previous draft opened the null backend and posted WM_CHAR. The null backend
is RENDER_CAP_HEADLESS, so editor_open (src/editor/open.c) creates no platform
and no HWND: there is nothing to post to. The fixture therefore offers both:

- `efx_open_null`: headless; `efx_type` uses `editor_inject` of the same key
  event; `efx_wait_presented` is a deterministic **simulated** completion (the
  null backend signals T5/T6 inside present). It never claims display timing.
- `efx_open_d3d11`: production D3D11 backend, a real hidden window on the private
  station; `efx_type` posts WM_CHAR; `efx_wait_submitted` is settle plus
  submitted_sequence!=0, the CPU-submit completion of an occluded surface (no
  T4/T6), labelled as such. Real present timing stays `SKIP (needs live desktop)`.

## The one production seam
`present()` in src/editor/editor.c, Windows branch, gated every frame on
`d3d11_present_ready()`. For a non-D3D11 backend `d3d11_descriptor()` fails, so
the gate returned false forever and a null-backend frame stayed `render_active`
(red run: `settle timeout pending=1 render_active=1`). The seam is one
expression: `undrawn` is `strcmp(b->info.name,"d3d11")==0 && !d3d11_frame_drawn(b)`
(the same name test `windows_surface` already uses). D3D11 behaviour is unchanged.

## Timeouts
Every wait is one `EFX_WATCH_NS` = editor_win_test's `WATCH_NS` (5 s, the editor
settle watchdog); a child run allows watches x WATCH_NS (3 = startup, test,
teardown, as before). Headless settle steps with a 20 ms bounded wait (the value
tests/editor_test.c settle_frame already passes to editor_step).

## Cleanup
`efx_profile_destroy` / editor_win_test `cleanup` remove a tree only when its
last component starts `sublimite-test-`; a child adopts a tree and never removes it.

## Durability oracle
`efx_journal_has_insert` reads the journal with `journal_scan_file` (CRC-checked,
read-only, independent of the editor) and requires the one-byte INSERT record
(8-byte offset + byte) and no open first-paint guard after it.
