# edit-m3g resolver 4

Resolved the five conflict hunks in `src/editor/open.c`,
`src/editor/private.h`, and `tools/embed_font.py`, retaining main's asynchronous
Unicode owner together with edit-m3g's DPI-derived sizing and worker zoom.
No `git` or `bd` commands were run, and no staging or rebase continuation was
performed. The [marker scan](../../build/edit-m3g-resolve4/markers.log) returned
1 with no matches.

## Embedded face and runtime integration

Kept main's `tools/embed_font.py` string-literal generator targeting
`src/font/embedded.h`. Removed `src/font/embedded.inc`. `embedded.c` exposes
that header's array through `font_embedded_data`, using `sizeof font_embedded - 1`
to exclude the string terminator. Both the editor's ASCII atlas worker and the
asynchronous Unicode worker borrow this same immutable array; the header is
included in only `embedded.c`.

[Byte verification](../../build/edit-m3g-resolve4/embedded-check.log) confirms
all 343140 bytes match `vendor/DejaVuSansMono.ttf`, SHA-256
`c805f9436dbc268644c1d9584f01a601a653e028e08fd74b9b949f6cf8304d88`.
The retained generator and generated header were not otherwise rewritten.

Joining the conflict sides alone would leave `font_async_open` and its binding
path restricted to the baked 15/30 pixel sizes. The asynchronous owner now also
accepts a worker-prepared ASCII atlas. Its primary and discovered fallback faces
use true-em sizing for display-derived fonts and retain pixel-height sizing for
legacy configurations. The cache borrows the matching ASCII atlas instead of
looking up an unrelated bake by pixel number.

Both font-size versions reserve a complete asynchronous owner at INIT, with
storage sized for the maximum prepaid cell. Zoom rebuilds the detached version
on the font worker. Reuse requires adopted completions and physical job
completion; switching requires backend retirement and the existing editor
quiescence conditions. Pending glyph work from the previous size continues to
drain through authenticated mailboxes without rebinding or damaging the new
size. Both versions retry queued submissions, and shutdown joins the pool before
freeing either owner or the ASCII atlas arenas.

`open.c` retains main's runtime page/glyph reserves, glyph-row tracking, lookup
callbacks and backend setup. Its atlas byte reserve covers the largest ASCII
band plus runtime pages; surface bounds and minimap storage follow edit-m3g's
font-independent window reserve and maximum cell size. `private.h` retains both
the runtime tracking and display/zoom state. Resize binds the adopted owner
before relayout, and size adoption clears old slot-to-row tracking.

## Build and verification

The initial combined build, subsequent rebuilds, and final `make all` all
returned 0. Release compilation used GCC; sanitizer compilation used Clang
ASan/UBSan. Both used
`-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`.

```sh
make -j4 all build/san/tests/font_test build/san/tests/editor_test \
  build/san/tests/minimap_test build/san/tests/font_size_test \
  build/san/tests/unicode_render_test build/san/tests/runtime_test \
  build/san/tests/x11_font_size_test
make all
```

Logs: [initial build](../../build/edit-m3g-resolve4/build.log),
[final combined build](../../build/edit-m3g-resolve4/final-build.log),
[final make all](../../build/edit-m3g-resolve4/make-all.log).

All sanitizer commands used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. Editor and native probes ran sequentially
through `tools/with_display_lock.sh` with `EDIT_DISPLAY_LOCK_TIMEOUT=0`.
The full suite, minimap and clipboard ran on `:99`. Subsequent commands encountered
a busy `:99` lock and retried on `:98`: save, startup, pending controls and the
native font-size probe. The manifest records both lock refusals and actual runs.
No ASan/UBSan diagnostics or compiler
warnings/errors were found in the validation logs.

| Probe | Exit |
| --- | --- |
| `build/san/tests/font_test` | 0 |
| `build/san/tests/minimap_test` | 0 |
| `build/san/tests/font_size_test` | 0 |
| `build/san/tests/unicode_render_test` | 0 |
| `build/san/tests/runtime_test` | 0 |
| `build/san/tests/editor_test --minimap-flash` | 0 |
| `build/san/tests/editor_test --clipboard` | 0 |
| `build/san/tests/editor_test --ck4-save` | 0 |
| `build/san/tests/editor_test --ck4-startup` | 0 |
| `EDITOR_REVIEW_ONLY=p4-12 build/san/tests/editor_test` | 0 |
| `build/tests/font_size_test` (release allocation guard) | 0 |
| `build/tests/unicode_render_test` (release allocation guard) | 0 |
| Full `build/san/tests/editor_test` | 1; failures below |
| `build/san/tests/x11_font_size_test` | 1; display initialization |

Commands, statuses and per-probe logs are recorded in the
[module manifest](../../build/edit-m3g-resolve4/modules.tsv) and
[editor/native manifest](../../build/edit-m3g-resolve4/editor.tsv).

The font-size probes retain DPI/Xft precedence, default/reset, retained frame
pixels, coalesced requests, outstanding-job shutdown, lower-bound renderer
capacity, and maximum-zoom/minimap geometry checks. Release zoom tests report
zero allocations with the counting guard active.

The Unicode suite retains main's initial/injected Unicode, combining ink,
wide-cell geometry, scalar/SIMD agreement, mailbox authentication, append-only
pixels, and completion-only damage checks. Added a cross-branch regression at
96 and 243 DPI: zoom/reset glyph pixels for embedded é and α match an
independently rasterized true-em primary, and combining artwork preserves base
ink. Holding the bulk worker through two size switches leaves both owners with
pending glyphs; reset waits for safe owner reuse while input continues to drain,
and completes after worker release. Existing held-backend tests verify borrowed
ASCII pixels survive font preparation until frame retirement.

Minimap flash reports 95 keys, 96 frames, `orange_frames=0`,
`changed_pending=0`, `chrome_markers=0`, and `mallocs=0`.

## Full-suite and native limitations

The full editor suite remains **unpassed**. It reports the previously recorded
launcher/reload failure (`reload launcher result=4`, `IPC_IO`) at
`tests/editor_test.c:1350`, and native `editor_open` failures at lines 741, 767,
1049 and 1705. It stops at `review_suite(NULL)` at line 2545, so subsequent
default-main cases are not reached. See the
[full editor output](../../build/edit-m3g-resolve4/editor-all.log).

The native font-size probe fails its initial
`editor_runtime_display_size(&display) == 0` assertion; see
[native font-size output](../../build/edit-m3g-resolve4/x11-font-size.log).
Fresh [socket probes](../../build/edit-m3g-resolve4/socket-access.log) return
`EPERM` for both filesystem and abstract X98/X99 connections and for AF_UNIX
bind/listen. Native display acceptance requires a socket-capable environment.
This evidence does not independently establish the cause of the socketpair-based
launcher/reload failure, which remains separately unresolved.

No full-suite, native screenshot/ink-height, or leak-check acceptance is claimed.
