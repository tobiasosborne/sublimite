# edit-a7fo worker report (s9)

## Cause

The wrapped layout's printable-ASCII fast path (`ascii_span`, `src/layout/wrap.c`)
drew cells but recorded no pointer hit records. Every ASCII cell kept the row
prefill written by `start_row` (the row's start byte). A click on wrapped ASCII
text therefore placed the caret at the row start, a click right of the text
placed it at the row end (the `end_row` fill), and a drag moved only between
row edges, so it selected whole rows. Wrap is on by default for untitled
buffers and `.md`/`.txt`/`.tex` files (`view_wrap_default`), so the trial hit
it; wrap-off files (for example `.c`) were never affected, and the existing
pointer tests all ran with wrap off. Full write-up and what was ruled out
(XI2 coordinates, 192 dpi cell size, gutter classification, menu-bar offset):
`docs/decisions/edit-a7fo.md`.

Reproduction: private Xvfb `:197` at 2880x1800 with `Xft.dpi: 192` (the trial
display's geometry and `xrdb` resource on :0), release `build/sublimite` on a
four-line `.txt` alphabet file, `xdotool` click then type. On main the typed
character landed at the row start for clicks at x=100 and x=300, and at the
row end for x=600. gdb on the same binary showed `hits[row 1][cells 2..27]` all
`{before=27, after=27}` (row start). After the fix the same probe put the
characters under the pointer and a drag selected `ghijklmn` by character.
The private Xvfb and the editor instance were stopped afterwards.

## Files touched

- `src/layout/wrap.c`: `ascii_span` records one `(at+i, at+i+1, within 0, width 1)` hit per drawn ASCII cell when the row is drawn (not sink or planning), as the unwrapped fast path in `layout.c` does.
- `tests/editor_test.c`: `a7fo_click_contract(wrap, menu, font_px)`: click at text column 4 of line 1 asserts caret byte 31, drag to column 10 asserts the selection 31..37, drag to line 2 column 2 asserts 31..56, a right-half click on column 7 of line 0 snaps to byte 8. Five variants: wrap on, wrap off, wrap on + menu bar, wrap on + menu bar + font_px 30, wrap off + menu bar + font_px 30 ((P) 30 = default 15 px x Xft.dpi 192 / 96; (M) 192 from `xrdb -query` on :0). In the main list and as `--a7fo-click`.
- `docs/decisions/edit-a7fo.md` (new), this report (new).

## Red run (before the fix; power Charging)

```
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:97 EDIT_DISPLAY=:97 EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 sh tools/with_display_lock.sh build/san/tests/editor_test --a7fo-click
editor_test:4101: FAIL editor_view(e).selection.cursor==27u+4u && editor_view(e).selection.anchor==27u+4u
exit 1
```

## Green run (after the fix; power Charging)

```
edit-a7fo click placement wrap=1 menu=0 font_px=0 passed
edit-a7fo click placement wrap=-1 menu=0 font_px=0 passed
edit-a7fo click placement wrap=1 menu=1 font_px=0 passed
edit-a7fo click placement wrap=1 menu=1 font_px=30 passed
edit-a7fo click placement wrap=-1 menu=1 font_px=30 passed
exit 0
```

## Verification (stamp: Charging; uptime 02:37 load average 26.78, 29.59, 24.90)

| Command | Exit |
|---|---|
| `make -j3 all` (gcc, -Werror) | 0 |
| san `editor_test` (display :97, lock, leaks on) | 0 (`editor_test: all passed`) |
| san `editor_scroll_test` | 0 |
| san `render_test` | 0 |
| san `layout_test` | 0 |
| san `raster_test` | 1, see below |
| release `build/tests/editor_test` (malloc guards active; G1 null typing mallocs=0) | 0 |
| release `build/tests/layout_test` (`0 allocations over 10000 relayouts (guard active)`) | 0 |

`raster_test` fails twice in a row on :97 at `render_test:229: FAIL
render_test_prepare(&b, &cfg) == RENDER_OK` inside `backend_test`, the first
check of the suite. That path is `plat_init` plus `render_cpu_backend` on a
4x6 fixture grid (`tests/raster_test.c:265`); it does not reach layout or
`wrap.c`, so I read it as an environment or pre-existing failure, not this
change. A diagnostic rerun on :99 waited on another holder's lock and was stopped
unrun; :97 advertises MIT-SHM, Present, SYNC, XFIXES and XInput.

## Side effect (same cause, now fixed)

`src/editor/highlight.c` builds syntax-highlight spans from the hit records
and skips `after <= before`; wrapped ASCII text had no usable spans, so syntax
colouring was missing for ASCII in wrapped buffers. It now gets spans as the
unwrapped path does. No test asserts this.

## Out of scope / notes

- `raster_test` backend_test failure above: the coordinator should rerun it on a free display, and on main, to place it.
- I ran one read-only `git diff --stat` in the worktree by mistake (no state change).
- Not covered: a non-ASCII wrapped line was already recorded by the general cluster path; no change there.
