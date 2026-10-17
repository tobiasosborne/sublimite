# edit-ptn worker report (session 8)

## Files touched

- src/utf8/utf8.h, src/utf8/utf8.c: `utf8_break_certain(x, y)`: a context-free UAX #29 break test.
- src/view/view.h, src/view/view.c: new `seek_scanned` probe beside `line_scanned`; `near_seed` uses
  utf8_break_certain and returns MORE when the budget is short; resumable `P_SEEK`
  (window seed, else walk back to the previous LF, never byte 0 unless it is the
  nearest proof) replaces every `scan_begin(near_seed())`; `P_REL_META` gives
  byte-relative Home/End/word/select-word/select-line in `view_relative_command`.
- src/editor/input.c: deletes' motions plus Home/End/word/select-line keys, and
  click/drag select word/line, route relative under `relative_repair`.
- tests/editor_large_test.c: `ptn_case` (CJK, long top line, keys) and its fixtures.
- tests/utf8_test.c: `t_break_certain`, checking soundness against utf8_grapheme_next.
- docs/decisions/edit-ptn.md.

## Red (probe extended, fix absent; at that point the extra bytes were added to line_scanned, later split into seek_scanned with the sum unchanged; release, `EDIT_PTN_REPRO=k build/tests/editor_large_test`)

df -h / before: 82G avail; after: 82G avail; no build/editor-large-ptn-* left.
```
edit-ptn key=x-in-cjk probe_bytes=18392701 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.000 ms (M) rc=1 overdue=1 power="Not charging" load1=57.81
edit-ptn case=0 rc=1 worst_probe_bytes=18392701 bound=4096
edit-ptn key=x-under-long-top probe_bytes=1538 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=2.576 ms (M) rc=0 overdue=0 power="Not charging" load1=57.81
edit-ptn key=Q-at-eof probe_bytes=512 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=3.536 ms (M) rc=0 overdue=0 power="Not charging" load1=57.81
edit-ptn key=home probe_bytes=960241920 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.000 ms (M) rc=1 overdue=1 power="Not charging" load1=57.55
```
(overdue=1: the key had not settled when the existing 10 s EDITOR_IDLE_NS watchdog
fired ((E): a watchdog, no gate behind it). The settle printed as 0 because it never
completed. The probe shows the byte-0 scan still running when the watchdog fired.)
Case (b), the long top line, did not reproduce: see the decision doc.

## Green (release, same binary path, same session)

df -h / before: 77G avail; after: 76G avail. Other jobs on the shared box were
writing at the time; no build/editor-large-ptn-* left.
```
edit-ptn key=x-in-cjk probe_bytes=1540 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=2.125 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
edit-ptn key=x-under-long-top probe_bytes=1540 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.932 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
edit-ptn key=Q-at-eof probe_bytes=512 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=9.181 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
edit-ptn key=home probe_bytes=512 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.765 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
edit-ptn key=end probe_bytes=513 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.243 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
edit-ptn key=ctrl+left probe_bytes=512 bound=4096 (P: VIEW_SCAN_BOUND) key_settle=0.177 ms (M) rc=0 overdue=0 power="Not charging" load1=55.95
```
uptime at the green run: load average 55.95, 62.16, 57.07 (a shared box under heavy load).

## Verification (exit codes)

- `make -j3 all` (gcc 13, -Werror): 0. A first run failed with -Wstringop-overflow
  on 3-byte arrays passed to utf8_encode in the new utf8 test; fixed by making them 4 bytes.
- `build/san/tests/utf8_test`: 0. `build/san/tests/view_test` (leaks on): 0, after
  splitting the probe: view_test:785 and view_fuzz assert zero line_query bytes on
  the relative restore, so the new categories went into `seek_scanned`.
- `build/san/tests/editor_test` on :99 under the display lock: 0.
- `build/san/tests/editor_scroll_test` on :99: one failure at editor_scroll_test:117
  (typing behind a held scroll worker, rc/alloc check), then 0 on two reruns: a
  load flake (load1 about 48).
- Release `build/tests/editor_test` on :99: 0, with 10 `mallocs=0` lines and no
  nonzero malloc line (the G1 malloc guard).
- `build/san/tests/editor_large_test`, full suite on :99: exit 1, both runs, at
  load1 42-49. Each run failed somewhere different, and neither failure is in the
  edit-ptn paths:
  - run 1: `cooperative_large_jobs` hit its 5 s deadline at line 931. Its early
    return left `build/editor-large-lane-*` (8 MB), which I removed.
  - run 2: `starved_follows` failed its G5 p99 assert (`ns <= EDIT_91C_G5_P99_NS`,
    9 ms) under ASan. The early return then gave an ASan stack-use-after-return,
    because a job still referenced the returned frame (a harness artifact).
  Run singly under ASan with leaks on, all of these pass: `EDIT_PTN_REPRO=0/1/2`
  (worst probe 1540 / 1540 / 513 bytes) and `EDIT_DZ0_REPRO` wrap=0 (line_query 0).
  `EDIT_91C_STARVED` passes with gate=TRACK: worst page submit 6.94 ms, Ctrl+End
  submit 7.02 ms.
- `EDIT_DZ0_REPRO EDIT_DZ0_WRAP=1` under ASan goes overdue at the Ctrl+End settle
  (line 1639), every run. **This was already failing:** a sanitizer build of the same
  test from the unmodified sources, made in scratch, fails identically twice.
  It passes in release, with line_query 3840 B as dz0 recorded.

df -h / around the sanitizer runs: 70G -> 68G avail (shared box). All ptn/dz0
fixture roots are removed on every path. The only leftover dir came from
cooperative_large_jobs' early return, and I removed it.

## What remains / out of scope (reported, not fixed)

- Brief item (2): stated as a bound in the decision doc, not seeded from lineidx
  chunks. Following still walks the cursor's whole line, about 279,620 slices
  for a 1 GiB line.
- Long-top-line red (b) did not reproduce on this tree: the top proof starts at a
  line start. It is kept as a regression case.
- Astra "Ctrl+End then Q visible wait": Q at EOF reads 512 probe bytes and settles
  in 3.5-9.2 ms (M) both before and after this fix (null backend, load1 about 56).
  The headless view repair does not reproduce the wait; it is likely elsewhere
  (frame, highlight or scroll), so it needs a trace on the real build.
- Already failing: dz0 wrap=1 times out in the sanitizer build. Wrapped Ctrl+End
  goes through `view_command(VIEW_DOC_END)` and a line query from byte 0, which is
  wrapped navigation and outside both edit-jf9 and this bead.
- editor_large_test early `T()` returns leak fixture dirs and leave worker jobs
  pointing at stack frames (ASan stack-use-after-return after a timing failure).
- `view_select_word` (the plan API used by input.c:584, Ctrl+D style) and
  `editor_large` find were not touched. SELECT_ALL / DOC_HOME need no metadata.
