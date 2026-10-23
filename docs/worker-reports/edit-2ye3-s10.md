# edit-2ye3 worker report (s10)

Bead: closing a modified tab gave no warning, and a modified closed tab was
evicted after 16 further closes. Decision note: `docs/decisions/edit-2ye3.md`.

## What changed
- **Prompt.** A user close (Ctrl+W; the menu and palette reach the same
  `input.c` close path) of a modified tab now shows
  `Save changes to <title>? S save / D don't save / Esc cancel` on the status
  row and leaves the tab open. S saves (Save As for an untitled buffer) and
  closes only after that save succeeds. D closes. Esc cancels. Unmodified tabs,
  Find Results and restore placeholders close at once, as before. Internal
  `editor_close_tab` calls (recovery.c, restore.c, tests) never prompt. Quit
  and window close are unchanged (hot exit).
- **Reuse of the source-change prompt.** Same row, theme colours and ASCII
  slots. Same key gating: `prompt_ns`/`prompt_serial` are stamped when the
  frame showing the prompt is submitted, so a key typed before that frame
  cannot answer it. State is embedded (`editor_close_prompt`, four
  `uint64_t`), with no allocation.
- **Eviction rule** (`tabs_close`). When the closed stack is full:
  1. Evict the oldest *unmodified* record.
  2. If every retained record is modified, an unmodified closing tab is not
     retained. Only its reopen is lost, never text.
  3. If the closing tab is also modified, the close is refused
     (`TABS_ERR_CAPACITY`, nothing changed). `editor_close_tab` returns
     `EDITOR_ERR_CAPACITY`, and the prompt's D drops only that tab (the
     user's say-so) through `tabs_drop`.

  Restore placeholders are marked modified before they are closed so that
  rule 1 does not evict them first.

## Files touched
- `src/tabs/tabs.c`, `src/tabs/tabs.h`: eviction rule and its contract comment.
- `src/editor/private.h`: `editor_close_prompt` state and declarations.
- `src/editor/buffers.c`: `close_tab` (with the discard drop),
  `editor_close_discard`, `editor_close_request`, `editor_close_ready`,
  `editor_close_service` (maintenance, after the file-command service). The
  prompt is dropped when another tab activates.
- `src/editor/input.c`: `close_prompt_key` (first in `editor_handle_key`); the
  close action calls `editor_close_request`.
- `src/editor/editor.c`: prompt stamp at submit, overlay call, runnable and
  pending while the post-save close can be decided.
- `src/editor/paint.c`: `editor_close_overlay`.
- `src/editor/restore.c`: placeholder marked modified before its close.
- `tests/editor_test.c`: `--close-prompt`, `--close-evict` (also in the
  default run).
- `tests/tabs_test.c`: `eviction_policy`. The model oracle (shared with
  `fuzz/tabs_fuzz.c`) now encodes the new rule.
- `tests/editor_p4_test.c`: its Ctrl+W on a modified tab now expects the
  prompt and answers D.
- `tests/editor_session_test.c`: `evicted_saving_tab`,
  `checkpoint_during_evicted_save` and `crash_after_evicted_checkpoint`
  evicted a *modified* saving tab, which the bead forbids. They now retire it
  the one remaining way (the pool holds a modified tab, then
  `editor_close_discard`). All other expectations, including disk bytes and
  recovery, are unchanged.
- `docs/decisions/edit-2ye3.md`, this report.

## Red run (tests first, before any src change)
```
$ ASAN_OPTIONS=detect_leaks=1 build/san/tests/tabs_test
tests/tabs_test.c:434: assertion failed: tabs_close(&s,0,&live,&evicted)==0 && evicted.id==ids[1]
exit=134
$ XDG_RUNTIME_DIR=$(mktemp -d) DISPLAY=:99 EDIT_DISPLAY_LOCK_TIMEOUT=1800 sh tools/with_display_lock.sh build/san/tests/editor_test --close-prompt
editor_test:6216: FAIL tabs_count(&e->tabs) == 1 && close_text_is(e, "xabc")
exit=1
$ ... build/san/tests/editor_test --close-evict
editor_test:6280: FAIL close_retained(e, kept)
exit=1
```

## Green run
```
tabs_test (san, leaks on)            exit=0  "tabs_test: eviction keeps modified closed records passed" ... "all passed"
editor_test --close-prompt (san)     exit=0  "edit-2ye3: modified close prompts (S saves+closes, D closes, Esc cancels); unmodified closes at once: PASS"
editor_test --close-evict (san)      exit=0  "edit-2ye3: eviction keeps modified closed tabs (17 later closes; all-modified pool): PASS"
```

## Verification (all ASan/UBSan with ASAN_OPTIONS=detect_leaks=1; display suites under tools/with_display_lock.sh, EDIT_DISPLAY_LOCK_TIMEOUT=1800, DISPLAY=:99)
| command | exit |
|---|---|
| `make -j3 all` (gcc 13, -Werror; rerun after the last test edits) | 0 |
| `build/san/tests/editor_test` (full) | 0 |
| `build/san/tests/editor_session_test` (after adaptation) | 0 |
| `build/san/tests/editor_hot_exit_test` | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/dataloss_test` | 0 |
| `build/san/tests/editor_p4_test` (after adaptation) | 0 |
| `build/san/tests/ipc_test` | 0 |
| `build/san/tests/cli_test` | 0 |
| `build/san/tests/editor_large_test` | 0 |
| `build/san/tests/tabs_test` | 0 |
| `build/fuzz/tabs_fuzz -runs=200000` (fresh corpus) | 0 |
| release `build/tests/editor_test --close-prompt` / `--close-evict` (DISPLAY=:97) | 0 / 0 |
| release `build/tests/editor_test` (full; `10000 keys mallocs=0 guard=active` for null, raster, gl-requested) | 0 |

Stamp for the release runs: `BAT0/status` = Not charging; `uptime` load
average 2.05, 2.24, 1.60. No timing number is claimed.

Malloc: the `--close-prompt` case brackets Ctrl+W (prompt shown) and Esc with
`edit_malloc_guard_begin/end` and asserts zero allocations. The guard is
inert under ASan and active in the release run, which passed.

editor_session_test includes the existing GB-scale orphan-launch fixture.
`df -h /` reported 87G free at session start and 86G free after the runs. I
did not take a reading right around that fixture.

## What remains
- I did not run a full `make check` across every suite. Only the suites
  named in the brief and those that call `editor_close_tab` or Ctrl+W were
  run (listed above).
- No message is shown when D drops a tab outright because the pool is full
  of modified tabs (rule 3). The drop is silent apart from the tab closing.

## Out-of-scope findings (reported, not fixed)
1. editor_session_test teardown hazard. When a `T()` fails while a save step
   hook is held, the test returns and its stack `held_save` dies while the
   worker still spins on it. The result is an ASan `unknown-crash` READ on the
   worker thread and a hung process. This was seen once, on the red-state
   `evicted_saving_tab` failure, and I had to kill it. That run also left
   `build/editor-session-DQCcWi`, which I removed.
2. An untitled buffer opened with initial bytes and no edits (for example
   content piped from stdin, if it arrives this way) counts as unmodified. It
   closes without a prompt and is the first to be evicted, so piped text can
   be lost after closes. Sublime would prompt here.
3. Closed tabs' `modified` flags are never refreshed after close. A peer save
   sets `saved_lost` on closed buffers, but only live tab flags are updated
   (session.c ~695, commands.c ~201), so eviction judges a closed tab by its
   flag at close time.
