# edit-5vz worker report (session 8)

Bead: find-in-files results-tab failures must never reach fail().
Decisions: docs/decisions/edit-5vz.md.

## Files touched

- `src/editor/fif.c`: `flush()` (an append refused with the tree unchanged
  sets `full`, no error), `truncate_full()` (stop assembly, cancel the job,
  append `Search truncated (results buffer full)`; status notice if even the
  row is refused), `notice()`; `assemble()` rolls `assembled` back to the
  first hit of the unstored batch; `summary()` goes through `flush()`;
  `editor_fif_start()` initialises the job locally, closes the panel and
  settles geometry, opens the results buffer, and only then retires the old
  job and submits. Refused open: no submit, notice
  `find in files: no results tab available`, rc 0.
- `src/editor/private.h`: `editor_fif.full`.
- `tests/editor_test.c`: `n5vz_results_full` (`--5vz-full`: a1 one refused
  request, a2 K-th request refused for every K until the result fits, a3 tight
  arena where every add-chunk request fails) and `n5vz_open_refused`
  (`--5vz-open`: b1 results tab active, b2 results tab inactive, retry);
  both added to the default run.
- `docs/decisions/edit-5vz.md`, this report.

## Red (tests first, unmodified src)

    $ DISPLAY=:99 ... with_display_lock.sh build/san/tests/editor_test --5vz-full
    edit-5vz: editor_step rc -104 error_cause 2
    editor_test:4865: FAIL n5vz_settle(e) == 0
    exit 1
(error_cause 2 = PIECE_ERR_NOMEM from the results append reaching fail(),
sticky EDITOR_ERR_HISTORY.)

    $ ... build/san/tests/editor_test --5vz-open
    [old results text, then the second search's 9 hits and summary appended]
    editor_test:4952: FAIL !strcmp(n95_text, old)
    exit 1

After the red run two test-side corrections were needed (no change to what
is asserted about the fix): the follow-up search in a1 expects the wide
fixture file too (`1033 matches across 4 files`, 9 + 1024 (P) counted from the
fixtures), and a2 now receives all hits first and drives assembly with
`editor_fif_service` alone, because with whole `editor_step`s the armed K-th
request could land on a scroll snapshot instead of an append (see out of
scope 1). a2 also accepts `assembled == L` (the refused append was the
summary's).

## Green

Power: Not charging [bat]; load average 40-50 on all runs (shared box).

    10:31:57 build/san/tests/editor_test --5vz-full
    edit-5vz: results append refusals truncate with a row or notice, never fail() passed   exit 0
    10:17:30 build/san/tests/editor_test --5vz-open
    edit-5vz: refused results tab submits nothing, keeps the old results, shows a notice passed   exit 0

## Verification

| command | exit |
|---|---|
| `make -j3 all` (gcc, -Werror), after the last source change | 0 |
| `ASAN_OPTIONS=detect_leaks=1 build/san/tests/fif_test` | 0 |
| `XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test` (leaks on), 10:44 | 0 (`editor_test: all passed`) |
| same, release `build/tests/editor_test`, 10:42 | 0 (`all passed`; `null 10000 keys mallocs=0 guard=active`, `null hl-on 10000 keys mallocs=0`) |

One earlier full sanitizer run (10:39) ended with LeakSanitizer reporting
7641596 bytes in 14 allocations from `raster.c cpu_init` via
`render_backend_init` on a work-pool worker (backend.c:33); the rerun at
10:44 on the same binary was clean. Not in this bead's code (see below).

The display lock was contended by other workers; runs needed
`EDIT_DISPLAY_LOCK_TIMEOUT=7200` (the default 600 s timed out twice).

## What remains

Nothing in scope. Other errors from `editor_results_open` (memory from
buffer preparation, activation errors) still propagate as before; the brief
named ERR_ARG and CAPACITY only.

## Out of scope (reported, not fixed)

1. `editor_scroll_slice` returns EDITOR_ERR_MEMORY when its piece snapshot
   allocation fails (scrollctl.c:48 path; gdb: fail() from editor.c:712), and
   editor_step passes it to fail(). Under real arena exhaustion of any buffer
   this kills the editor regardless of edit-5vz.
2. Opening the results tab (editor_activate) and then changing the text
   geometry (panel close, `editor_text_geometry`) in the same turn made the
   next step's `editor_scroll_slice` return -1 (gdb: editor.c:712) when the
   results tab was re-selected from another tab. Avoided by keeping the old
   geometry-then-open order; the underlying activate/resize interplay was not
   investigated.
3. Intermittent LeakSanitizer report from `raster.c cpu_init` (lines
   377-454) via `render_backend_init` on a worker, seen once in a full
   sanitizer editor_test run, absent on rerun.
