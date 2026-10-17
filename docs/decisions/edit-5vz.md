# edit-5vz: find-in-files results-tab failures never reach fail()

Code: `src/editor/fif.c`, one flag in `src/editor/private.h` (`editor_fif.full`).
Tests: `tests/editor_test.c` `n5vz_results_full` (`--5vz-full`) and
`n5vz_open_refused` (`--5vz-open`), both in the default run.

## (1) Refused results appends truncate

- **Which failures count as "full".** `editor_results_append` refuses before
  touching the tree when the line index is at capacity, or when `piece_insert`
  fails (piece.h: on failure the tree content is unchanged; the editor's piece
  allocator returns NULL when the buffer arena is exhausted). `flush()` treats
  an rc with `piece_len` unchanged as "results buffer full": no error, assembly
  stops. An rc after the tree grew comes from the shared change bookkeeping
  (`changed()`: layout, scroll, frame), the same path every typed edit
  propagates; it is not a results-capacity condition and keeps its existing
  meaning.
- **Line maps.** Hits staged after the last stored append are dropped with
  their bytes: `assemble()` remembers the first hit of the unflushed batch and
  resets `f->assembled` to it on a refusal, so every map entry below
  `assembled` points into stored text (the old code incremented `assembled`
  before the final flush).
- **The truncation row.** `truncate_full()` sets `full` and `summary_written`
  (nothing else is appended for this search), cancels the engine job (its
  further hits could not be shown), and appends
  `Search truncated (results buffer full)` after a blank line, like the
  summary. If the buffer refuses even the row (every allocation fails), the
  same text is shown as the status-row notice (edit-btj `recovery_notice`,
  cleared at the next mutation). Neither path returns an error. Appending
  instead of overwriting the last row: overwriting needs a delete plus an
  insert through the same allocator and gains nothing when the row fits.
- A refused summary append takes the same path (row after the hits).

## (2) The results buffer opens before the search is submitted

`editor_fif_start` order is now: parse Where; refuse if a retiring job is
still finishing; `fif_job_init` into a local job (filter errors refuse with
the old results untouched); close the find panel and settle the text
geometry (as before, for the current buffer); `editor_results_open`; only
then retire the old job, move the local job into `current`, reset counters
and submit. Opening the results tab before the geometry change was tried
first: the step after it failed in `editor_scroll_slice` (rc -1, gdb
backtrace editor.c:712) when the results tab was re-selected, so the old
geometry-then-open order is kept.

- `EDITOR_ERR_ARG` (activation busy) or `EDITOR_ERR_CAPACITY` (no tab or
  buffer slot) from the open: dispose the local job, count a rejected command,
  show the notice `find in files: no results tab available`, return 0. The old
  results text and the old job stay as they were; the panel is closed as on
  a successful start (Ctrl+Shift+F reopens it with query and Where kept). Other open
  errors propagate as before.
- `fif_submit` failing after the open (work slots busy) leaves the fresh empty
  results tab, counts a rejected command and shows
  `find in files could not start`.
- A `fif_job` is a single impl pointer and is not referenced by the work pool
  before `fif_submit`, so initialising it locally and copying it into the slot
  is safe (the old code copied live slots into `retiring` the same way).
