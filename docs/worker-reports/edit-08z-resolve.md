# edit-08z rebase resolver report

Resolved all three conflict hunks in `src/editor/open.c`. No `git` or `bd`
command was run; the rebase was not advanced.

## Resolution

- Retained edit-d5o's `map_capacity`, `map_pixel_capacity`, compact minimap
  grid/damage allocations, `map_pixels` and `map_scratch`, allocation checks,
  and renderer `max_region_width` / `max_region_height`.
- Retained edit-08z's `font_async_limits`, glyph-use row storage and arena
  reservation, `font_async_open`, bindings to the full/text grids, and runtime
  renderer glyph/page/pixel limits. The async owner supplies the ASCII atlas
  as well, so the superseded fixed ASCII owner initialization was removed.
- The arena reservation includes both minimap pixel/grid storage and glyph row
  storage. Minimap initialization uses `map_capacity` and `map_words`, matching
  the actual allocations and the two-pixel summary geometry.
- Clipboard initialization and the existing shutdown order remain intact.
  No other production source was edited.

Final marker scan:
`rg -n '^(<<<<<<<|=======|>>>>>>>)( |$)' src tests` returned 1 (no matches).

## Build and validation

Logs are in `build/edit-08z-resolve/`. Both builds passed (exit 0):

```sh
make -j"$(nproc)" all
make -j"$(nproc)" build/san/tests/editor_test build/san/tests/font_test build/san/tests/minimap_test build/san/tests/unicode_render_test
```

Release compilation used GCC; sanitizer compilation used the Makefile's Clang
ASan/UBSan configuration. Both used
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.

Every test below used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`.

| Command | Exit | Log / result |
| --- | --- | --- |
| `build/san/tests/editor_test` (full default suite, display wrapper below) | 1 | `editor-all.log`; native display cases and launcher close reply failed |
| `build/san/tests/editor_test --minimap-flash` | 0 | `editor-minimap-flash.log`; 95 keys, 96 frames, zero orange frames, zero changed-pending pixels, zero chrome markers |
| `build/san/tests/unicode_render_test` | 0 | `unicode.log`; initial and injected é, α, 日, 🙂, widths, combining overlay, atlas pixels, scalar/SIMD agreement, and owner/mailbox checks passed |
| `build/san/tests/font_test` | 0 | `font.log`; all passed, including worker-prepared Unicode ownership/adoption |
| `build/san/tests/minimap_test` | 0 | `minimap.log`; pixel geometry, density, stale state, hit mapping, and deferred/cache checks passed |
| `EDITOR_REVIEW_ONLY=p4-16 build/san/tests/editor_test` | 1 | `editor-reload-identity.log`; isolated rerun reproduced launcher result 4 (`IPC_IO`) |

The full editor command was:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=0 \
sh tools/with_display_lock.sh build/san/tests/editor_test
```

It acquired the display lock and executed the suite. The headless minimap pixel
composition, minimap flash, clipboard, pending-worker controls, recovery,
large undo/replay, source-change, and backend-owner initialization cases passed.
Native editor initialization failed at `tests/editor_test.c:741`, `:767`,
`:1049`, and `:1705`. Direct AF_UNIX connection probes to both filesystem and
abstract X99 sockets returned errno 1 (`Operation not permitted`), confirming
the sandbox prevents native display access. An independent AF_UNIX bind/listen
probe also returned errno 1.

The launcher/reload case separately failed at `tests/editor_test.c:1350` with
`reload launcher result=4`; its isolated rerun reproduced the failure. This
report does not establish that this socketpair-based failure is caused by the
display restriction. It was also recorded in the earlier branch validation
report. No IPC or test code was changed during conflict resolution.

There were no ASan/UBSan diagnostics in these test logs. The full editor suite
is **not green**; its failure also stops main before later default-suite cases.
Native acceptance and the launcher failure remain for the coordinator to check
with the required display/socket access. ASan malloc guards are inert, so their
zero counters do not establish release allocation behavior.
