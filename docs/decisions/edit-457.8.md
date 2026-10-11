# Hot exit session integration

The recovery boundary is an applied editor action, independent of whether its
frame is ready to submit. Journal metadata at action completion and at successful
public tab/cursor operations. Preserve pending metadata across journal credit
exhaustion; staged text must precede metadata describing that text. Submission
continues to enforce the recovery fence. All encoding and cache storage is
reserved before typing; no new worker, allocation, sleep or sync is added there.

Retain the legacy VIEW payload and add a full-state payload under the same
record kind, discriminated by validated payload length. The legacy prefix stays
unchanged. The extended fields are preferred column, logical first line, visual
first byte and flags for wrap, approximation and cursor affinity. Wire size is
derived from the existing four unsigned byte/column fields plus four additional
unsigned fields, each encoded as a little-endian uint64_t: (G, schema)
`(4 + 4) * 8 = 64` bytes. Legacy payload is (G, existing schema) `4 * 8 = 32`
bytes. Checkpoints and save captures use the same encoding as ordinary metadata.

Restore before platform/backend initialization. Apply the recovered grid geometry
before publishing the replacement checkpoint, and create the platform window at
its recovered pixel dimensions. Pixel dimensions remain independent of the
grid's whole-cell dimensions so a partially filled edge cell does not shrink the
window on every restart. Legacy view records derive the logical first line from
their byte offset and use the file's default wrap mode.

Crash acceptance uses the bead's requested (G) `100` headless trials. Script
length reuses that same acceptance count rather than inventing another limit.
Each seeded trial selects its kill boundary modulo that script length, types
through it, changes session metadata, publishes its in-memory oracle to the
parent, and is killed with SIGKILL without teardown. The parent restarts from the
journal and compares bytes, live order, active tab, every persisted view field,
and geometry before any frame and after settling. A separate production CLI
display trial is exercised by the coordinator on its permitted Xvfb display.

Recovery sizing after font integration: `editor_resize_grid` updates the prepaid
editor grids before backend creation and calls `render_backend_resize` only
when the backend is initialized. `editor_open` then passes the recovered grid
dimensions to backend initialization. Runtime resizing retains the renderer's
capacity/busy checks. The hot-exit oracle checks backend configuration as well
as editor dimensions before the first frame; raw pixel dimensions remain in
`e->font.window_width` and `e->font.window_height`.

## Window size policy at restart (landing, 2026-10-11, coordinator decision)

Precedence: **explicit launch size > journalled chosen size > DPI default.**

- An explicit launch size (`editor_config.window_width/height` with
  `window_size_default` false) wins per axis; this keeps edit-a6v's guarantee
  that the created window, the grid and the painted surface agree.
- Native startup (src/main.c) passes its DPI-derived size with
  `window_size_default = true`, so a journalled size chosen by the user's last
  run restores over it (the hot-exit CLI trial resizes, kills -9, restarts and
  expects the size back).
- WINDOW record v2 (G, wire schema): `width32 height32 flags32` = 12 bytes,
  flag bit 0 `JOURNAL_WINDOW_CHOSEN`, always set by this build (session
  capture, checkpoint, metadata append, `journal_set_window`). The decoder
  `journal_decode_window` accepts both sizes the way `journal_decode_view_state`
  does; unknown flag bits are invalid. An 8-byte record decodes with flags clear.
- Legacy 8-byte records hold older builds' grid pixels (960x600 from the
  pre-DPI default, 1720x1240 from a6v-era builds at 243 dpi), not a size anyone
  chose, so they never resize the window: install_session keeps the DPI default
  (or the caller's existing size).
- editor_resize_now clamps any requested size to the reserved
  `font.max_width/max_height`, so a journal written on a larger display cannot
  size the window beyond the grid and raster surface.

Evidence: window_size_test red before the policy change
(`window_size_test:130` legacy record restored 1373x911 over the DPI default;
build/edit-457.8-v2/red-window_size.log), green after; journal_test
`window_schema` covers both sizes, the unknown-flag rejection and the v2
writer; journal_fuzz replays both sizes. On :98 (2880x1800, 243 dpi) with a
copy of the real ~/.local/share/sublimite journals (8-byte, 960x600) the window
opens at 1728x1260 and is fully painted (build/design/457-8-dpi243.png); an
a6v-era journal (8-byte 1720x1240) also opens at 1728x1260 and is rewritten
as a 12-byte chosen 1728x1260 record.
