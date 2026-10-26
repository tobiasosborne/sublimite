# edit-v4ee: display queries and portable keysym names

The platform owns startup display discovery. `plat_display_size(backend, info)`
returns `plat_display_info` (DPI and work-area dimensions); the duplicate editor
structure and editor query entry point are removed. `main.c` and the native
font-size regression use this shared type. The pure DPI, Xft parsing, point-size
and launch-window helpers remain in `src/editor/display_size.c`, declared by
`editor/runtime.h`.

The X11 query and RandR timing bodies are moved into `src/x11/x11_posix.c`,
wrapped whole for the POSIX build. Their requests, ordering, arithmetic,
fallbacks and evidence comments stay intact. The necessary substitutions are
function/type names, platform error codes and the platform pointer replacing
its editor owner. A null platform/connection returns unknown timing. The editor
keeps its existing `has_platform` check. `main.c` translates an X11 discovery
failure back to `EDITOR_ERR_IO`, preserving its Linux startup diagnostic.
RandR linkage moves from `src/editor/LDLIBS` into `src/x11/LDLIBS`.

The Wayland size branch delegates to `wayland_display_size` and maps its DPI
and physical buffer dimensions into the shared result. Its startup error codes
and diagnostics remain unchanged. Refresh timing stays unknown on Wayland,
matching the previous editor helper. Both new platform entries are documented
as INIT only; the editor retains the existing first-service timing query.

Keysym constants come from the unchanged vendored header, with original licence
notices retained. Package provenance is in `vendor/README.md`. Relative quoted
includes keep these constants usable with the existing Linux and future Windows
include paths, including nested prewake fixtures.

`tools/keysymgen.py` reads the pinned header without consulting a library,
system header or network. It emits the committed `src/keys/keysym_names.h`;
`--check` compares regenerated output with that file. Names are sorted by ASCII
case-folded spelling, then original spelling. A second array indexes canonical
names in numeric keysym order. Binary search in each direction is deterministic,
requires no allocation and avoids adding a hashing dependency. All aliases
remain in the name table; the first header declaration for a value determines
its canonical output name.

Case-insensitive lookup preserves the installed libxkbcommon's results. Each
collision group normally selects the spelling with the fewest ASCII uppercase
letters. The pinned library's otherwise ambiguous non-letter exceptions select
`dead_I`, `dead_O`, `dead_SCHWA`, `kana_YA`, `kana_YO` and `kana_TSU` (M: the
Linux oracle compares every header name and its uppercase/lowercase variants).
These winners are explicit in the generator rather than depending on incidental
binary-search positions. `XF86ScreenSaver` also resolves to the library's
`XF86Screensaver` value through the normal spelling rule. Updating the pinned
header/library requires rerunning the complete oracle before changing winners.

Numeric spellings are procedural rather than table rows. Preserve XKB's `U`
Unicode notation, case-insensitive prefix, printable Latin-1 handling, and
Unicode-namespace encoding, including its surrogate/noncharacter acceptance.
`U+XXXX` is rejected by both implementations. Raw `0x` notation is accepted
within the X11 keysym range. Numeric spellings allow at most the hexadecimal
digit capacity of `uint32_t` (P: its bit width divided by bits per hex digit).
Legacy `XF86_` aliases, including repeated underscores after the prefix, resolve
without allocating. Formatting preserves canonical aliases, Unicode notation,
raw hexadecimal fallback, invalid-value results, full return lengths and buffer
truncation. Null argument handling is explicitly defined by the new API.

The Linux keys test links libxkbcommon unconditionally through a per-target
Makefile addition; the oracle and generated test tables are excluded on Windows.
Production keys/config objects no longer reference any `xkb_*` function. Config
retains its existing direct-byte path for single-character strokes and its
normalization rules. The independent default-keymap expectations still use the
Linux library as their oracle, and config parsing is compared with that oracle
for named, literal, numeric, alias and invalid strokes.

The strict acceptance grep still finds XCB includes in `src/gl/renderer.inc`.
The brief explicitly requires reporting that renderer dependency without fixing
it. The coordinator must reconcile the grep criterion with that scope exclusion;
this bead does not change GL or raster presentation.

## Coordinator decision (session 22, 2026-10-26)

**`src/gl` is platform-side.** The acceptance grep's exclusion list was x11, wayland and raster; `src/gl/renderer.inc` keeps its xcb includes (Present, native connection) because the GL backend is Linux-only by design (docs/design/windows-port.md §5.3: GL through WGL was not chosen; Windows gets a D3D11 `render_backend_ops` in W1.2). The waiver is recorded on the bead. The acceptance rule from here on: no X11, xcb or xkbcommon library include outside `src/x11`, `src/wayland`, `src/raster` (XShm present, W1.2) and `src/gl`. The worker's Codex session ended with `ERROR: Selected model is at capacity` during its final full check (which was also waiting on the :99 display lock held by W1.0b's check); the coordinator ran the full check on :95 (worker report, coordinator section).
