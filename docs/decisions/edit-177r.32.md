# Resize presentation on Windows

Tobias's acceptance is "it should be visually instantaneous". The target is the
next refresh, (P) 1000 / 165 Hz = 6.060606 ms. This is a display requirement,
not a timing result. Windows execution is prohibited by the revised brief;
visible behaviour and the deadline are **not run / unverified** on this tree.

## Size-to-present path

1. `WM_WINDOWPOSCHANGED` first calls DefWindowProc to generate `WM_SIZE`.
   It no longer services an expose turn using the old geometry first.
2. `WM_SIZE` updates the current client extent and coalesces its event. A
   borrowed callback runs synchronously while the editor is idle or suspended
   at its ordinary native pump boundary. Callback depth, servicing, resizing,
   and the editor step guard prohibit reentry from layout/render callbacks or
   lifecycle pumps. Unsafe callbacks retain their event for the next safe turn.
3. The callback goes directly through existing geometry, scroll resolution,
   layout, paint preparation, composition, CPU snapshot and presentation. It
   does not drain queued typing or run file/find/restore/palette/checkpoint,
   font-discovery, worker-adoption or durability maintenance before painting.
   Those ordinary-turn responsibilities remain intact. Resize uses the current
   prepared atlas and existing reservations; there is no new typing allocation.
4. Old dirty layout is replaced. A D3D resize supersedes the active old-size
   snapshot without waiting for T5/T6, retaining its consumed frame id and
   requiring full replacement damage. Late completions cannot release the new
   slot. The DwmFlush worker uses its immutable frame generation and no longer
   reads a mutable pending present count. No completion is fabricated.
5. ClearState, releasing the owned backbuffer reference, `ResizeBuffers`, and
   `GetBuffer` remain necessary native work. Immediate-context ordering and
   native references retain GPU lifetimes. Scene, target, textures and their
   views keep their prepaid extents. A sub-cell resize still changes the chain.
6. Visible layout and decoration preparation remain necessary. Layout keeps
   the existing EDITOR_SLICE_NS bound (G: inherited 500000 ns). If it yields,
   a coalesced private native message continues paint without waiting for
   USER_TIMER_MINIMUM. This also allows an unsuccessful DO_NOT_WAIT Present
   to retry. No timer, sleep, worker join or blocking GPU query is added.
7. Window/view journal metadata is deferred to the normal turn, so remembering
   the new size does not delay drawing it. Admitted text operations, first-paint
   protection, save/clipboard ownership and continuations borrowing the old
   layout retain their necessary fences. Resize retries do not flood User32
   while those owners need to progress. Device/OOM rebuilds wait until native
   servicing unwinds, following edit-177r.31.
8. Submit copies immutable cell/atlas/map values. Draw uploads those values,
   draws active cells, copies the retained scene, paints margins and copies
   only the current client box into the backbuffer. The redundant clears of
   both entire reserved targets are removed: full damage already overwrites
   active cells/overscan, and copy plus margins covers the client.
9. A new-size frame bypasses the old latency credit and calls Present1 with
   sync interval zero and DO_NOT_WAIT. Ordinary frames retain interval one and
   the waitable. The GPU event is observed nonblocking; authenticated T5/T6
   observation happens later. GPU availability, DXGI and DWM composition plus
   the physical refresh still stand between submission and visible pixels.

These choices remove avoidable queue/timer/frame-completion and maintenance
waits by mechanism. They cannot establish the next-refresh deadline on the
RTX 3090 without the coordinator's authorized run.

## Stretch and exposed pixels

The flip chain uses `DXGI_SCALING_NONE`. DWM therefore does not rescale the old
buffer when the client changes. `SetBackgroundColor` supplies the theme base
for uncovered pixels. The initial colour is copied on UI at platform binding,
then handed to the exclusive init worker by value. It never reads mutable UI
theme state during init. Each real draw updates the chain colour.

The platform also fills `WM_ERASEBKGND` with the theme base using a stock DC
brush. It receives that colour before mapping and during composition. The
window class continues to have no independent background brush. The margin
shader covers partial cells. No Linux implementation branch changes.

## Verification design and pointer isolation

The source pointer guard first rejected the incoming unguarded SC_SIZE case.
It now passes. A dedicated real-loop helper starts with
`LIVE_ONLY_NATIVE_LOOP`, and `--resize-live` is included in the live selector.
Hidden resize coverage uses programmatic SetWindowPos and direct
WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE notifications, including the pump-boundary
callback registration. This checks servicing policy without a real native
pointer loop. Neither that case nor any other Windows executable was run in
this revised continuation. See edit-177r.33 for the shared-pointer finding.

Fixtures assert no-stretch via GetDesc1; surface extent at SetWindowPos return;
fresh drawn dimensions after queued bounded continuations; dark/light margins,
GDI erase and DXGI background colour; old drawn-frame supersession, missing
latency credit, stale completion rejection and deferred inline OOM recovery.
The continuation count bound comes from resident ASCII fixture cells/rows plus
prepare/submit/present; it is a liveness bound, not a refresh latency assertion.
`--resize-profile` prints a stage table and QPC derivations for the coordinator.
Hidden WARP is not Tobias's RTX 3090; software draw/driver costs, occlusion
Present costs and blocking diagnostic readback do not transfer to visible
hardware presentation. Windows runtime red/green and stage measurements on the
final tree are not run.

## Minimap investigation

No minimap defect was established. Inspection of the reservation change in
open.c shows that capacity is font-independent, while paint.c still composes
current `map_cols * cell_w` by `visible_rows * cell_h` pixels with compact row
stride. D3D snapshots those rows, uploads that current box into the reserved
texture and copies it without scaled sampling. GL/raster use the same current
region geometry. Reserved dimensions do not determine the viewport band.

The existing fixed-scale model uses MINIMAP_LINE_PX per logical line. Band
height is min(document lines, visible rows) times that scale, clipped to strip
height; total lines determine scroll range. The new Linux test loads the same
C source as the Windows fixture, resizes through startup/full/shrink/grow,
and compares editor pixels against an independently prepared minimap and the
portable raster kernel. It passes under sanitizers. The hidden D3D fixture is
ready to compare its current region with that kernel at startup, resize,
maximize, changed DPI, and on short/empty documents; those Windows comparisons
are not run. Platform fonts can have different cell metrics, so a whole-window
byte comparison between different font rasterizers is not claimed.

There is no evidence for a minimap implementation change. If the report persists,
the coordinator needs Tobias's screenshot identifying the viewport band or map
image, file, zoom/DPI and window size.
