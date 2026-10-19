# edit-dxas (s9): Save As then Ctrl+S no longer reloads and drops undo

## Cause
The Save As target's path watch (buffers.c, `editor_poll_sources`) took the
inotify event of the controller's *own* Ctrl+S rename before the job's result
(the new baseline) reached the UI, compared the target with the stale
baseline and flagged FILE_MSG_OPEN_FAILED/FILE_ERR_CHANGED. After the save the
buffer was clean, so `editor_check_sources` ran the clean-refresh
`reload_source`, which replaces the editor_buffer with an empty undo history.
See docs/decisions/edit-dxas.md.

## Fix
The watch is left pending while `savectl_get_model(b->save).busy`; it is
taken in the first turn after the result is adopted and compared with the
baseline the job wrote. External changes are still detected (see decision).

## Files touched
- src/editor/buffers.c (one deferral condition before the target-watch take)
- tests/editor_session_test.c (`save_as_then_save_body`, `save_as_then_save`,
  `dxas_all`; `--edit-dxas` option; added to the default run)
- docs/decisions/edit-dxas.md, this report

## Variants
All eight reproduce: journal on/off x COPY/MMAP (threshold `sizeof ""`) x
new/existing target.

## Red run (before the fix)
```
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=3600 ASAN_OPTIONS=detect_leaks=1 \
  sh tools/with_display_lock.sh build/san/tests/editor_session_test --edit-dxas
edit-dxas: journal=off mode=copy target=new replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=off mode=copy target=existing replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=off mode=mmap target=new replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=off mode=mmap target=existing replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=on mode=copy target=new replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=on mode=copy target=existing replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=on mode=mmap target=new replaced=yes undone=no
editor_session_test:233: FAIL !replaced
edit-dxas: journal=on mode=mmap target=existing replaced=yes undone=no
editor_session_test:233: FAIL !replaced
exit=1
```
(A first red run, with the diagnostic reading `b->file` after the replacement,
hit ASan heap-use-after-free on the retired buffer freed by
`editor_retire_buffers`: the same reload. The test now records the mode first.)

## Green run
```
edit-dxas: journal=off mode=copy target=new replaced=no undone=yes
edit-dxas: journal=off mode=copy target=existing replaced=no undone=yes
edit-dxas: journal=off mode=mmap target=new replaced=no undone=yes
edit-dxas: journal=off mode=mmap target=existing replaced=no undone=yes
edit-dxas: journal=on mode=copy target=new replaced=no undone=yes
edit-dxas: journal=on mode=copy target=existing replaced=no undone=yes
edit-dxas: journal=on mode=mmap target=new replaced=no undone=yes
edit-dxas: journal=on mode=mmap target=existing replaced=no undone=yes
exit=0
```

## Verification (BAT0: Full; 05:05 load 12.38/11.45/11.16; display :99, leaks on, outside sandbox, EDIT_DISPLAY_LOCK_TIMEOUT=3600)
| command | exit |
|---|---|
| make -j3 all (gcc, -Werror) | 0 |
| build/san/tests/editor_session_test (full default run, includes dxas_all) | 0 |
| build/san/tests/dataloss_test | 0 |
| build/san/tests/savectl_test | 0 |
| build/san/tests/undo_test | 0 |
| build/san/tests/editor_test | 0 |
| build/tests/editor_test (release, malloc guard active: "clipboard malloc guard: 0 allocations (active)") | 0 |

The dataloss_test ze59 line printed `Z input->submit=2.001 ms ... G1 p99=2.000 ms`
on a sanitized build under load 10.64 (M)[AC Full]; the gate is enforced on
release only (ze59 convention), tracked, not a blocker.

## Remaining / out of scope
- The edit-3620 worktree's test (`kept_target_unreadable`) ends with a Ctrl+S
  commented "a clean reload may replace b"; once both land, that comment is
  stale (no change made here, it is not on main).
- Not checked: whether the same early-event race applies to the original
  file watch (`b->file`, `file_changed`) of a non-rebound buffer; existing
  repeated-save tests pass, so no symptom observed.
