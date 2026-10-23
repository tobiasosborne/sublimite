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

## Round 2 (review docs/reviews/edit-2ye3.md, on commit 4f9c7ba)
I wrote one red test per item first (`tests/editor_test.c`,
`--close-review-<item>`; `--close-review` runs all five, as does the default
run). All five reproduced.

### Red run (before any src change)
```
--close-review-hidden  exit=1  editor_test:6342: FAIL tabs_count(&e->tabs) == 1 && e->go.active && e->go.length == 1 && e->go.digits[0] == 'd'
--close-review-wide    exit=1  editor_test:6356: FAIL render_grid_validate(editor_grid(e)) == RENDER_OK && !e->stats.render_rejects
--close-review-refused exit=1  editor_test:6379: FAIL press(e, key('w', PLAT_MOD_CTRL, NULL)) == 0   (editor_step failed: fatal)
--close-review-scroll  exit=1  editor_test:6400: FAIL e->scroll_plan.origin_y_q8 < 0 && !e->grid.scroll.origin_y_q8 && !e->grid.region.pixels
--close-review-stdin   exit=1  editor_test:6415: FAIL tabs_count(&e->tabs) == 2 && close_status(e, "Save changes to untitled?")
```
1. The hidden case uses the mouse menu's own call: `editor_dispatch_action`
   with `menu_command(KEYS_ACTION_SAVE_AS)`.
2. The wide case uses a file named `中.txt`. It also checks that a prompt
   whose frame was dropped (`prompt_ns` cleared) lets the next key through.
3. The refused case simulates a placeholder: the `placeholder` flag plus a
   modified tab flag, as restore.c sets them, with a one-record closed pool
   holding a modified tab.

### Fixes
1. **Hidden prompt.** The close prompt owns keys only while it is the visible
   owner of the row (`editor_close_prompt_shown`): no go-to/Save As prompt,
   palette, menu focus or open drop-down. Otherwise it is cancelled at the
   next submit (overlay) or key, with a full recompose. It never resumes; the
   reason is in the decision doc.
2. **Wide titles.** Non-ASCII graphemes draw as '?', one ASCII cell each, so
   the row is always contract-valid. `prompt_ns` is stamped only when a
   submitted frame showed the prompt. An unstamped prompt holds keys only
   while its frame is pending (`e->dirty`); otherwise it is cancelled and the
   key handled.
3. **Refused close.** Every user close path goes through `user_close`
   (direct close, D, post-save close). On `EDITOR_ERR_CAPACITY` the tab stays
   open with the notice `close refused: every closed tab holds unsaved text; reopen one (Ctrl+Shift+T) first`.
   Nothing reaches `editor_step` as fatal.
4. **Scroll and minimap.** While the prompt draws, `grid.scroll` and
   `grid.region` are disabled for the frame, as the source-change prompt does
   (full damage when either was active).
5. **Stdin buffers.** `close_unsaved`: an untitled, non-derived,
   non-placeholder buffer with text counts as modified for the prompt. Any
   close marks its tab modified first, so eviction keeps it.

### Files touched (round 2)
- `src/editor/buffers.c`: `close_unsaved`, `user_close`,
  `editor_close_answer_discard`, `editor_close_prompt_shown`.
- `src/editor/input.c`: visibility and unstamped rules in `close_prompt_key`.
- `src/editor/editor.c`: the stamp requires a shown prompt.
- `src/editor/paint.c`: overlay rewritten (ASCII/'?', scroll and region off,
  cancel when hidden).
- `src/editor/private.h`: declarations.
- `tests/editor_test.c`: five new cases. `clipboard_no_tabs_test` and
  `review_stale_controls` closed untitled buffers with text by Ctrl+W; they
  now answer D.
- `tests/dataloss_test.c` (window variant) and `tests/editor_large_test.c`
  relied on evicting untitled buffers with text, which item 5 forbids. Each
  now retires the intended buffer by `editor_close_discard` after asserting
  that the plain close is refused. dataloss keeps slot 2 as the retired leaf,
  so the empty-branch shape it tests is unchanged.
- `docs/decisions/edit-2ye3.md`: "Round 2" section.

### Green run (sanitizers, ASAN_OPTIONS=detect_leaks=1, display suites on :99 under with_display_lock.sh)
| command | exit |
|---|---|
| `editor_test --close-review-{hidden,wide,refused,scroll,stdin}`, `--close-prompt`, `--close-evict` | 0 each |
| `make -j3 all` (gcc, -Werror; after the last edit) | 0 |
| `build/san/tests/editor_test` (full) | 0 |
| release `build/tests/editor_test` (full; `10000 keys mallocs=0 guard=active` for null, null hl-on, raster, raster-requested) | 0 |
| `build/san/tests/tabs_test` | 0 |
| `build/san/tests/editor_p4_test` | 0 |
| `build/san/tests/editor_session_test` | 0 |
| `build/san/tests/editor_hot_exit_test` | 0 |
| `build/san/tests/editor_checkpoint_test` | 0 |
| `build/san/tests/dataloss_test` (after adaptation) | 0 |
| `build/san/tests/editor_large_test` (after adaptation) | 0 |
| `build/san/tests/ipc_test` | 0 |
| `build/san/tests/cli_test` | 0 |

The first full pass failed four times before the test adaptations: editor_test
:3940, dataloss_test :882 (and :152 in the parent), editor_large_test :2341.
Each failure was one of the untitled-with-text closes listed above.

Stamp for the release run: BAT0 Not charging; load average 1.60, 1.08, 0.92.
`df -h /` 86G free after the runs. No timing is claimed.

### Remaining / backlog (not fixed, as instructed)
- Review finding 5: a peer save leaves a closed tab's modified flag stale.
- No notice when D drops a tab outright.
- The editor_session_test hang after a failed check with a held save.
- A full `make check` was not run; the suites above were.
