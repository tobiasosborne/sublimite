# edit-cxho.25 (s24): view jumps while typing so the caret's line lands on the top row

Worker: Claude Opus 5.5, worktree `.wt/edit-cxho.25-s24`, display :95. Decision note:
`docs/decisions/edit-cxho.25.md`.

## Result

Reproduced red first, then fixed. All failures are in **wrapped** mode (the default for .md/.txt/.tex);
unwrapped never failed. Five shapes, all of them the stale-row family edit-6090 opened:

| Named case (deterministic, `c25_named`) | Red on main |
| --- | --- |
| letter typed at the end of the bottom row's line (mid-file) | JUMP: idx 26 -> 0, top 420 -> 966 |
| Return at the start of a soft-wrapped (continuation) row on the bottom row | JUMP: idx 26 -> 0 |
| two Returns before one frame at the bottom row (key repeat, fast Return-Return) | JUMP: idx 26 -> 0 |
| two Returns before one frame at the end of the file, caret mid-screen | JUMP: idx 10 -> 0 (last line becomes the top row) |
| paste of four lines at the bottom row | JUMP: idx 26 -> 0 |
| three BackSpaces before one frame at the bottom row | JUMP: idx 26 -> 0 |
| Down onto a soft row end (trailing affinity) below the bottom row | caret off screen (top one row below it) |

The end-of-file case matches Tobias's picture exactly: two keys landing in one frame (a fast paragraph
break, or any key while the frame is late) make the second key see a restarted layout whose cached
rows read 0. `cursor >= 0` then holds, so the follow treats the caret as a far jump and its row (the
file's last line) becomes the top row.

Mechanisms (details in the decision note):
1. `view.c finish_move` (edited): `cursor >= wrap_rows[last].next`. That `next` is stale, the old line
   end + 1, so a single letter typed at the end of the bottom row's line reaches it.
2. `input.c finish_edit`: `cursor.start > wrap_rows[last].start` plus the edit-6090 one-row proof,
   which fails when the caret's row stays on the bottom row (Return at a continuation-row start) and
   when the caret moves more than one row (paste, Return burst).
3. When the layout is busy (several keys before a frame), `layout_wrap_edit` restarts it and the cached
   rows are zeroed. The non-edit LEFT of BackSpace and the edit paths then read `bottom == 0`.
4. Non-edit trailing affinity: the follow used the byte's row, not the row above it where the caret
   is drawn.

## Reproduction (seed / key index)

The random run is `editor_test --cxho25-typing`. Each run is identified by its extension, wrap mode, CRLF,
placement, paste kind and seed (`seed*7919 + place*104729 + mode index`); the key index is the first
violation. Red examples from the final red run below:
`.txt wrap place 2 crlf seed 225297 key 0 (Return x1)`,
`.c wrap place 1 seed 112652 key 0 (letter x1)`,
`.md wrap place 0 seed 7919 key 27 (Return x4)`,
`.c wrap place 3 seed 322110 key 8 (paste)`.
Key indices can shift by a few keys between runs: undo groups typing by time
(`edit_time_ns`), so an undo key removes more or less text depending on the box's speed. That is why
the minimised cases are the seven named deterministic ones (no undo, no timing).

## Fix

- `src/layout/wrap.c`, `src/layout/layout.h`: new `layout_wrap_follow_edit(l, t, top, caret_row,
  max_scroll, &new_top, &moved, &approx)`. It walks fresh rows up from the caret's row to the top or to a
  certified cached row: rows `[0, l->row)` when busy after `layout_wrap_edit`, all rows when idle, none
  when stale. That gives the caret's distance d below the top.
  - d < rows: keep the top.
  - d up to rows-1+max_scroll: minimal top, with the caret on the bottom row.
  - farther: the caret's row becomes the top (the old far-jump policy).
- `src/view/view.c finish_move`: every edit, and every motion while the layout is busy, follows through
  the helper (`fresh_follow`). An edit uses max_scroll = rows-1; a motion uses 1, the cached path's
  one-row rule. The cached non-edit path now follows the row on which a trailing-affinity caret is
  drawn. `one_row_below` (edit-6090) is removed.
- `src/editor/input.c finish_edit`: uses the same helper, with trailing affinity, instead of the stale
  bottom compare. `wrapped_one_row_below` (edit-6090) is removed.
- `tests/editor_test.c`: `cxho25_typing_jump` (selector `--cxho25-typing`, also in the default run).
  It runs the seven named cases plus 48 seeded runs: 6 path/mode combinations (.md, .txt, .c,
  extension-less by default, plus .md forced unwrapped and .c forced wrapped) x 4 placements (true end
  of file, end of line on the bottom row, mid-line on the bottom row, the row above the bottom) x 2
  seeds (LF + block paste + Return 35 %, CRLF + long-line paste + Return 10 %), 400 keys each.
  - Keys: letters, spaces, Return (bursts of 2-4 before a frame), Tab (auto-indent lines in the
    fixture), BackSpace (bursts), paste, undo, redo, arrows/Home/End, wheel notches, and resizes to
    heights that are not whole rows.
  - After every frame it checks the invariant (`c25_judge`). The caret must stay visible. The top
    must not move while the caret stays on screen. If the caret is less than a screen below, the
    scroll must be minimal (caret on the bottom row, never on the top row).
  - Bounds (M) [bat]: every red run failed by key 68, so 400 keys per run is ~6x that. Runtime is
    6.7 s release, 16.9 s ASan. `C25_SEEDS` and `C25_KEYS` lengthen a soak.

## Red run (sources reconstructed to main's finish_move/finish_edit; same final test)

```
Not charging
 10:42:51 up 1 day, 12:46,  1 user,  load average: 1,92, 2,03, 1,81
exit 1
edit-cxho.25 named case 'letter at the end of the bottom row's line': JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 26 -> 0 k 26 top 420 -> 966
edit-cxho.25 named case 'Return at the start of a continuation row on the bottom row': JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 26 -> 0 k 26 top 1995 -> 2587
edit-cxho.25 named case 'two Returns before a frame at the bottom row': JUMP: caret re-anchored on the top row; rows 27 idx 26 -> 0 k 28 top 420 -> 988
edit-cxho.25 named case 'two Returns before a frame at the end of the file': JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 10 -> 0 k 12 top 5250 -> 5462
edit-cxho.25 named case 'paste of four lines at the bottom row': JUMP: caret re-anchored on the top row; rows 27 idx 26 -> 0 k 30 top 420 -> 1070
edit-cxho.25 named case 'three BackSpaces before a frame at the bottom row': JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 26 -> 0 k 26 top 420 -> 966
edit-cxho.25 named case 'Down onto a soft row end below the bottom row': caret off screen; rows 27 idx 26 -> -1 k 27 top 2884 -> 3566
edit-cxho.25: ext .md wrap 1 crlf 0 place 0 long_paste 0 seed 7919 key 27 (Return x4): JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 12 -> 0 k 16 top 41519 -> 41970 ...
edit-cxho.25: ext .txt wrap 1 crlf 1 place 2 long_paste 1 seed 225297 key 0 (Return x1): JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 26 -> 0 k 26 top 16055 -> 17542 ...
edit-cxho.25: ext .c wrap 1 crlf 0 place 1 long_paste 0 seed 112652 key 0 (letter x1): JUMP: top moved to the caret row although it stayed on screen; rows 27 idx 26 -> 0 k 26 top 17390 -> 18809 ...
edit-cxho.25: ext .c wrap 1 crlf 0 place 1 long_paste 0 seed 112653 key 16 (Return x2): JUMP: caret re-anchored on the top row; rows 27 idx 26 -> 0 k 28 ...
(24 of 24 wrapped runs red; 0 unwrapped)
editor_test:6329: FAIL failed == 0
edit-cxho.25: 7845 keys checked, 31 violations
```

## Green run

```
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:95 EDIT_REFUSE_REAL_DISPLAY=1 sh tools/with_display_lock.sh build/tests/editor_test --cxho25-typing   # exit 0
edit-cxho.25: 15012 keys checked, 0 violations
edit-cxho.25: random typing at the bottom rows never re-anchors the caret on the top row
```
Soak run: `C25_SEEDS=10 C25_KEYS=1500` gave 283606 keys checked and 0 violations, in 2 min 5 s release.
An earlier soak of the first fix (16 seeds x 1500 keys) found two further shapes: BackSpace over a
busy layout, and Down onto a trailing end. Both are fixed above and covered by named cases.

## Verification (box: [bat], load ~1-4, another lane sharing the box)

- `make -j4 all` exits 0.
- ASan, with `ASAN_OPTIONS=detect_leaks=1`, display :95 via `tools/with_display_lock.sh`. Every
  command exits 0.
  - `build/san/tests/editor_test --cxho25-typing`: 15012 keys checked, 0 violations (16.92 s).
  - `--6090-typing` (edit-6090 regression): passed (58.33 s).
  - `--vp4m-return`: passed.
  - `layout_test` all passed; layout no-malloc: 0 allocations over 10000 relayouts.
  - `view_test` all passed.
- Full `make -j4 check` (DISPLAY=:95) exits 0: `check: 110 test binaries passed`. Also passed:
  `tools/test_replay_cli.sh`, and the display lock contract (it waited on :99, which another lane
  held, then PASS). The os_test `AddressSanitizer: BUS` line in the log is that suite's intentional
  stray-fault child ("a child now takes an unregistered fault on purpose"; the suite reports
  `fault: ok`).
- Malloc guard (counting allocator):
  - The new test asserts `count.allocations == 0` for every run and named case.
  - In `make check`: `editor_test: null 10000 keys mallocs=0` and `edit-2ty typing_path_allocations=0`.
  - In the G1 bench below: `allocations=0` on every pass.
- G1 typing latency. `tests/editor_test.c` has no G1 selector (as edit-9f63's landing report also
  found); the G1 row is in `bench/editor_bench`.
  - Setup: before and after binaries, back to back in ABBA order, on a wrapped fixture (HANDOFF.md
    copied to `build/g1-fixture.md`). Command: `EDIT_BACKEND=raster editor_bench --no-idle --no-p4
    --serial-only --keys=2000 --file=build/g1-fixture.md` on :95.
  - Power was `Charging`/`Not charging` across passes. The bench's own `[AC]` tag is printed
    regardless of power.
  - Times are (M), p50/p99 in ns. n=2000 is below the bench's required_n=10000, so the bench reports
    TRACK/REFUSED, not a gate verdict.

| pass | start | null G1 p50 / p99 | raster G1 p50 / p99 | load1 |
| --- | --- | --- | --- | --- |
| before #1 | 11:02:59 | 613227 / 753490 | 16683240 / 18475241 | 0.97 |
| after #1 | 11:04:08 | 441122 / 542123 | 16694414 / 18168142 | 1.52 |
| after #2 | 11:05:16 | 470899 / 860917 | 16685938 / 18250500 | 0.94 |
| before #2 | 11:06:25 | 733113 / 1347098 | 16798280 / 33389449 | 1.85 |

  After is no slower: null p50 is 441-471 us against 613-733 us before. The likely cause is that
  typing in this fixture triggered the old jumps, and every jump re-anchors and fully relays the
  viewport. Raster is dominated by its ~16.4 ms submit and is unchanged.

## Files touched

- `src/layout/wrap.c`, `src/layout/layout.h`: `layout_wrap_follow_edit`.
- `src/view/view.c`: `fresh_follow`, `finish_move`; `one_row_below` removed.
- `src/editor/input.c`: `finish_edit`; `wrapped_one_row_below` removed.
- `tests/editor_test.c`: `cxho25_typing_jump` and helpers (`c25_*`), registered as `--cxho25-typing`
  and in the default run.
- `docs/decisions/edit-cxho.25.md`, and this report.

## What remains / hypotheses not covered

- `view.c relative_wrap_place` (P_REL_WRAP: lazy large files whose line count is not exact) keeps the
  same shape: edited with `cursor >= wrap_rows[last].next` gives `visual_byte = r.start`. It cannot fire
  on Tobias's small Markdown files (it needs `lg.lazy && !lines_exact`), and its fresh queries need the
  edit-wv7 anchored path, so I left it alone. It is the next suspect if a jump is reported in a
  GB-scale wrapped file.
- A partially visible bottom pixel row is covered only through resizes to heights that are not whole
  rows (rows = floor(height / cell_h)). The null backend cannot show a partially painted row.
- Periodic UI-thread events (journal tick, index publish, retention) were not re-tested here.
  edit-6090 ruled them out for unwrapped typing. None of the wrapped failures above needed them, and
  the bursts reproduce Tobias's picture without them.

## Found outside scope (reported, not fixed)

1. `view_wrap_default` (src/view/view.c) returns **false** for an extension-less path. The `dot &&`
   check fails when there is no '.', so `README` or `notes` opens unwrapped. The bead and brief say
   extension-less files wrap by default. The function also uses `strrchr(path, '.')` over the whole
   path, so a dot in a directory name (`/home/x.y/notes`) is matched against the extension list.
2. `editor_bench` stamps `(M)[AC]` on every row even while `/sys/class/power_supply/BAT0/status` reads
   `Not charging`.
3. While the layout is busy, `layout_begin` leaves `wrap_rows` zeroed rather than `LAYOUT_VOID_ROW`.
   Other readers of `wrap_rows[n-1]` during a busy layout could misread it the same way finish_move did
   (for example the `relative_wrap_place` path above). I did not audit them.
