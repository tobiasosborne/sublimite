# edit-3f2k: save admission and session ranks skip derived and placeholder tabs

recovery-3 F1 (BLOCKER). Session publication (`publish`, src/editor/session.c)
never gives a Find Results (derived) or recovery placeholder buffer a leaf, but
save admission compared the published live count with **all** live tabs, and
live leaves carried the raw tab index as their TABS rank. With Find Results
open, every Ctrl+S was refused (`FILE_ERR_NOMEM` from `request_buffer`), and a
recorded tab after the derived one would leave a gap in the dense ID array
`encode_all` requires (`JOURNAL_INVALID` in the PREPARE checkpoint).

## Choice

The review's smallest fix, as written: one mapping, the one `recorded_tabs`
(src/editor/checkpoint.c) already uses for the UI checkpoint's TABS record.

- `editor_recorded_rank(e, index)` (checkpoint.c, declared in private.h):
  number of live tabs before `index` that TABS records, i.e. neither
  `placeholder` nor `derived`. `index = tabs_count` gives the recorded count.
- `editor_session_metadata`: a live leaf's rank is a running count of recorded
  tabs (dense); closed leaves keep their closed-stack index (unused by TABS).
- `editor_session_publish` (first publication, no old leaf): rank is
  `editor_recorded_rank(active index)`, not the raw active index.
- `request_buffer` admission: `live_count != editor_recorded_rank(e, tabs_count)`.
- Active index: unchanged. `encode_tree` sets `active` to the rank of the leaf
  whose ID is the active tab's ID, default 0; when the active tab is derived it
  stays 0, the same value `recorded_tabs` writes in that case.

The predicate is duplicated between `recorded_tabs` and `editor_recorded_rank`
(two lines) rather than refactoring `recorded_tabs`; scope rule.

## Test

`n3f2k_save_with_results` in tests/editor_test.c (next to n95, selector
`--3f2k-save`), journal-backed so the save runs the PREPARE checkpoint
encoder: a.txt (tab 0), Find Results (tab 1), nested/b.c (tab 2, recorded
rank 1). Ctrl+S on b.c, then on a.txt; both files hold the typed byte and the
tabs are clean. A variant with only the count check fixed (raw ranks) fails
this test, confirming the review's note that sparse ranks also break encoding.
