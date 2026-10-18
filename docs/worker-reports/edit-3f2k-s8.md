# edit-3f2k (s8): a live Find Results tab no longer blocks Ctrl+S

recovery-3 F1. Decision record: docs/decisions/edit-3f2k.md.

## Files touched

- src/editor/checkpoint.c: new `editor_recorded_rank(e, index)` (same filter as `recorded_tabs`: skips placeholder and derived tabs).
- src/editor/private.h: its declaration.
- src/editor/session.c: `editor_session_metadata` gives live leaves dense ranks; `editor_session_publish` first-publication rank is the recorded rank of the active index; `request_buffer` admission compares `live_count` with the recorded count, not `tabs_count`.
- tests/editor_test.c: `n3f2k_save_with_results` (+ helpers `n3f2k_saved`, `n3f2k_file`) after `n95_find_in_files`, selector `--3f2k-save`, added to the default run.

## Red

Unfixed tree, first version of the test (no journal):

    editor_test:5024: FAIL e->buffer->save_error == 0 && !editor_tab(e, 2u)->modified   exit=1

The test was then made journal-backed, so the save runs the PREPARE checkpoint encoder (without a journal savectl never calls `prepare`, so `encode_all` was not reached). With only the count check fixed and raw tab-index ranks:

    editor_test:5029: FAIL e->buffer->save_error == 0 && !editor_tab(e, 2u)->modified   count-only exit=1

This bears out the review's note: fixing only the count still leaves the sparse-rank encode failure. The unfixed tree fails at admission, before the journal matters. The final journal-backed test was not re-run against the fully unfixed tree.

## Green

    edit-3f2k: Ctrl+S on files before and after a live Find Results tab passed   fixed exit=0

## Verification

Stamp: BAT0 `Not charging`; `uptime` load average 27.31 / 47.06 / 41.35 at 12:22. No timings were measured.

| command | exit |
|---|---|
| make -j3 all (gcc -Werror) | 0 |
| make -j3 build/san/tests/{editor_test,editor_session_test,editor_checkpoint_test,dataloss_test} build/tests/editor_test | 0 |
| XDG_RUNTIME_DIR=$(mktemp -d) EDIT_DISPLAY_LOCK_TIMEOUT=1800 ASAN_OPTIONS=detect_leaks=1 DISPLAY=:99 sh tools/with_display_lock.sh build/san/tests/editor_test | 0 |
| same, build/san/tests/editor_session_test | 0 |
| same, build/san/tests/editor_checkpoint_test | 0 |
| same, build/tests/editor_test (release, malloc guard) | 0 |
| ASAN_OPTIONS=detect_leaks=1 build/san/tests/dataloss_test | 0 |

journal_test was not run because src/journal was not touched. editor_session_test contains the existing GB-scale orphan fixture. `df -h /` was not captured before that run. After it: 480G size, 403G used, 53G available (89 %).

## Cleanup

The red and count-only runs returned early from `T()` and left their fixtures behind. I removed them: /tmp/editor-3f2k-P0xaiU, /tmp/fif-tree-tU4gb9 and /tmp/fif-tree-yoqUsQ. I left the eleven older /tmp/fif-tree-* directories (08:55 to 10:29) alone; they are not from this bead.

## Out of scope (reported, not fixed)

- `T()` failure paths in n95-style tests leak `/tmp/fif-tree-*` fixtures. Eleven older trees from other runs are sitting in /tmp.
- With a journal, a live tab whose buffer is still `pending_open` is counted by `recorded_tabs` but never published, so save admission still refuses every save while such a tab exists. This predates the bead and is unchanged by it.
- gcc -Wshadow flags a local named `journal` in tests (a global by that name is visible); clang -Wshadow does not. I renamed my local.
