# edit-3kk resolver report

Resolved all 20 conflict blocks in the nine files identified in the brief.
`rg -n '^(<<<<<<<|=======|>>>>>>>)' src bench tests` finds no remaining markers.
No git or bd commands were run; the rebase/index was not advanced.

## Merged render layout

`src/render/render.h` keeps both independent contracts in `render_grid`, in
this declaration order:

```c
typedef struct render_scroll {
    int64_t origin_y_q8;
    uint32_t first_row, rows, cols;
} render_scroll;

typedef struct render_pixels {
    const uint32_t *pixels;
    size_t capacity, stride;
    uint32_t x, y, width, height;
    bool dirty;
} render_pixels;

typedef struct render_grid {
    render_dims dims;
    render_cell *cells;
    size_t cell_capacity;
    uint64_t *dirty;
    size_t dirty_word_capacity;
    const render_atlas_page *pages;
    size_t page_count;
    const render_glyph *glyphs;
    size_t glyph_count;
    uint32_t frame_id;
    bool full_frame, begun;
    render_pixels region;
    render_scroll scroll;
} render_grid;
```

Measured with GCC and the requested C11 warning flags on this x86-64 workspace:

| Type | Size | Alignment | Relevant byte offsets |
| --- | ---: | ---: | --- |
| `render_grid` | 160 | 8 | dims 0; cells 16; cell_capacity 24; dirty 32; dirty_word_capacity 40; pages 48; page_count 56; glyphs 64; glyph_count 72; frame_id 80; full_frame 84; begun 85; region 88; scroll 136 |
| `render_pixels` | 48 | 8 | pixels 0; capacity 8; stride 16; x 24; y 28; width 32; height 36; dirty 40 |
| `render_scroll` | 24 | 8 | origin_y_q8 0; first_row 8; rows 12; cols 16 |
| `render_config` | 120 | 8 | max_region_width 104; max_region_height 108; background_init 112 |
| `editor_frame` | 248 | 8 | scroll 48; selection 96; wrap_seed 120; sidebar_revision 224; sidebar_identity 232; wrapped 240 |

Scroll applies only to the indicated text columns and visible rows. Its exact
Q8 origin remains in `(-cell_h*256, 0]`; enabled scroll reserves and validates a
full-width source row at `dims.rows * dims.cols`. The pixel region uses fixed
surface coordinates. The minimap starts at whole-cell coordinates
`x = text_cols * cell_w`, `y = tab_rows * cell_h`, with height
`(surface_rows - tab_rows) * cell_h`. Its height excludes text overscan.

Raster jobs render scrolled text first, then composite the owned pixel region
using surface scanline bounds. GL draws transformed/clipped text first, then
`gl_blit_region` converts the fixed region rectangle to GL's bottom-left
coordinates. Neither region blit adds the text origin.

## Preserved and integrated behavior

- Open-time cell, row metadata, wrap, and font-row-bit reservations include the
  bottom overscan row. Main's minimap grids/pixel buffers and async glyph/page/
  atlas limits remain reserved alongside it.
- `editor_buffer` retains both `path` and `save_path`, plus scroll/display
  states, both resolvers, resident bridge, snapshot lease, revision/cursor/
  prefix metadata, and ready/pending/following flags.
- `editor` retains the generation-owned unimplemented notice, scroll/submitted
  plans, geometry generation, two immutable refresh frame records, and refresh/
  completion bookkeeping. Clipboard polling, selection capture, action gates,
  save submission, startup stage traces, and bootstrap/probe adoption coexist
  with scroll continuations and refresh hooks.
- GL state retains region texture/FBO/storage, region fence/job/generation/
  pending state, and scroll state plus clip/motion uniforms. Existing init
  generation handling in `gl_driver.h` and staged initialization remain intact.
- Glyph-only composition refreshes the overscan snapshot; fractional damage
  becomes full surface damage. Async glyph tracking includes the overscan row.
  Chrome scratch grids clear both optional render extensions.
- Displayed-frame sidebar identity includes the submitted minimap pixels,
  rather than only its now-background-only sidebar cells. Present metadata
  uses the adapter's actual T4 timestamp.
- `scroll_bench` keeps `--display-check` and its unavailable physical-evidence
  report, plus the editor `on_refresh` displayed G3z probe.

## Validation

`make all` and `make build/bench/scroll_bench` succeed with GCC. Sanitizer targets
build with Clang ASan/UBSan. Project code uses
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`; the existing vendored
tree-sitter warning exceptions are unchanged. Every sanitizer run below used
`ASAN_OPTIONS=detect_leaks=0`. Display-dependent runs used `DISPLAY=:99` and
`tools/with_display_lock.sh`.

| Requested run | Result |
| --- | --- |
| `build/san/tests/editor_test` (all) | Exit 1: socket-dependent checks blocked by sandbox, detailed below |
| `build/san/tests/editor_test --minimap-flash` | PASS |
| `build/san/tests/editor_test --clipboard` | PASS |
| `build/san/tests/editor_test --ck4-save` | PASS |
| `build/san/tests/scroll_test` | PASS |
| `build/san/tests/render_test` | PASS |
| `build/san/tests/raster_test` | Exit 1: live visual fixture cannot initialize X11 |
| `build/san/tests/minimap_test` | PASS |
| `build/bench/scroll_bench` build | PASS |

Additional ASan/UBSan passes: `editor_scroll_test`, `raster_scroll_test`,
`gl_scroll_test`, and `scroll_display_test`. The added regression checks cover
fractional text with an independent sidebar pixel region in raster, fixed GL
blit destinations, and editor region geometry without overscan leakage.

The full raster invocation passed its pixel-region snapshot/map-only worker
test and review sections P2-1-15, P2-1-11, P2-1-8, P2-1-7, 1, 4, and 2 before
reaching the blocked live visual fixture. Separate `raster_test --pixels-only`
and `--review` sections 5, 6, 7, 8, 15, 17, and G11 also pass.

Release `render_test`, `editor_scroll_test`, `raster_scroll_test`, and
`gl_scroll_test` pass. The active release allocation guard records zero
allocations over 10,000 render frames and in the held-worker scroll/typing
checks; sanitizer allocation guards are inert.

`build/bench/scroll_bench --displayed-probe` exits 0:
10,000 synthetic opportunities, zero misses, and all 5,000 injected previous-ID
repeats detected. It reports `displayed_verdict=SYNTHETIC_PASS` and
`physical_verdict=UNSUPPORTED`; native completion alone is not an every-refresh
opportunity stream.

### Sandbox limits on the two full suites

The all-editor run fails the real launcher wait check at `editor_test.c:1350`
(`reload launcher result=4`, IPC_IO) and native editor-open checks at lines 741,
767, 1049, and 1705. A separate `EDITOR_REVIEW_ONLY=p4-16` run reproduces the
launcher failure. A minimal local `socketpair().send()` probe returns
`[Errno 1] Operation not permitted`, establishing the IPC transport restriction.

The raster run fails at `raster_test.c:1125`, where `plat_init` requires a live
display. `xdpyinfo` cannot connect to shared `:99`. A private `xvfb-run` retry
also fails: Xvfb cannot bind its listening sockets, and a direct connection to
the private display's Unix socket returns `EPERM`. These restrictions prevent
claiming either full suite passed. No socket-dependent tests were disabled or
weakened; both full suites still need a rerun in a socket-enabled environment.

Execution logs and the compiled struct-layout probe are available under
`/tmp/edit-3kk-*.log` for this session.
