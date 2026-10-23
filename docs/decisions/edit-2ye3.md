# edit-2ye3: close prompt for modified tabs; eviction keeps modified closed tabs

## Prompt
A user close (Ctrl+W, File menu, palette: all reach `input.c` through
`editor_dispatch_action`, now `editor_close_request`) of a modified document
tab no longer closes it. It shows, on the status row,
`Save changes to <title>? S save / D don't save / Esc cancel`.
Unmodified tabs, Find Results (derived) and restore placeholders close at
once as before. Internal closes (`recovery.c`, `restore.c`, tests and the
`editor_close_tab` API) never prompt. Quit and window close are unchanged
(hot exit, no prompt).

Reused from the source-change prompt (edit-1um, edit-3620): the bottom status
row, its theme colours (`warning.foreground` on `chrome.background`), the
ASCII atlas mapping, and its key gating. `prompt_ns`/`prompt_serial` are
stamped when the frame that shows the prompt is submitted
(`editor.c`, next to the source prompt's stamp); only keys dispatched after
that frame answer it, so a key typed ahead of Ctrl+W's frame cannot discard
a file. While the prompt is up it owns the keyboard (checked first in
`editor_handle_key`); repeats and Ctrl/Alt/Super chords are swallowed, key
releases pass through so modifier bookkeeping still runs. State is one
embedded `editor_close_prompt` (four `uint64_t`): no allocation.
The overlay is drawn after the save overlay and before the go-to/Save As
prompt; the title is cut so the choices stay on screen; non-ASCII title
graphemes use the Save As prompt's resident font path.

The prompt belongs to its tab: activating another tab drops it
(`editor_activate`).

- **Esc**: dismiss, recompose the row; tab stays open and modified.
- **D**: close. The closed tab still keeps its text and history for
  Ctrl+Shift+T when the pool has room (the existing retention; nothing is
  lost that was not lost before).
- **S**: the normal save (`editor_save_request`), or the Save As prompt for an
  untitled buffer (no path). `close_prompt.saving` remembers the tab; a
  maintenance step (`editor_close_service`, after the file-command service)
  waits until no Save As path is being typed or queued and no save, Save As
  write or checkpoint is in flight (`editor_save_active`), then closes the tab
  only if it is unmodified and has no save error. A failed save, a cancelled
  Save As, a refused request (stale source) or edits typed meanwhile leave the
  tab open. While that decision is ready the loop counts it as runnable and
  pending, so it is taken without further input.

## Eviction rule (tabs_close)
When the closed stack is full:
1. evict the **oldest unmodified** retained record;
2. if every retained record is modified and the closing tab is unmodified,
   the closing tab is **not retained** (returned as evicted and retired);
   only its reopen convenience is lost, never text;
3. if every retained record is modified and the closing tab is modified,
   `tabs_close` **refuses** (`TABS_ERR_CAPACITY`, nothing changed);
   `editor_close_tab` returns `EDITOR_ERR_CAPACITY` and the tab stays open.
   The prompt's **D** is the user's say-so for that one tab: it is dropped
   (`tabs_drop`, retired) instead of retained; the retained modified tabs are
   untouched.

Reason: this is the smallest rule under which no modified closed tab is ever
evicted without the user's say-so, with no new storage and no growth of the
fixed pool. Internal closes already check closed capacity before closing
(recovery.c, restore.c), so they never reach case 3. Restore placeholders
(deferred large journals) are now marked modified before they are closed,
since they stand for unsaved journalled edits; otherwise case 1 would evict
them first. The tabs model oracle (`tests/tabs_test.c`, shared with
`fuzz/tabs_fuzz.c`) encodes the same rule.

## Tests
`tests/editor_test.c` `--close-prompt`, `--close-evict` (also in the default
run); `tests/tabs_test.c` `eviction_policy`.

## Round 2 (review docs/reviews/edit-2ye3.md)
- **Another prompt cancels the close prompt.** The prompt owns the row and
  the keys only while it is the visible owner (`editor_close_prompt_shown`):
  no go-to/Open/Save As prompt, palette, menu focus or open drop-down. When
  any of those takes over (for example the mouse menu's File > Save As), the
  close prompt is cancelled. The overlay cancels it at the next submit, the key
  handler at the next key, and a full recompose follows. It never resumes. The
  user presses Ctrl+W again. Cancelling is chosen over yield-and-resume
  because a resumed prompt would answer keys the user typed for the other
  prompt's purpose, and a cancelled one loses nothing (the tab stays open).
- **Title drawing.** Non-ASCII title graphemes draw as one '?' cell each (the
  source-change prompt is ASCII-only). Every cell is then a one-cell
  resident ASCII cell, so the row is always render-contract-valid. Wide
  titles no longer get a frame rejected.
- **No swallowed keys without a shown prompt.** `prompt_ns` is stamped only
  on a submitted frame that showed the prompt. Without the stamp, a key still
  waits while that frame is pending (`e->dirty`). If no frame is pending (the
  frame was dropped), the prompt is cancelled and the key is handled normally.
- **Refused user close.** Every user close (Ctrl+W/menu/palette direct close,
  D, the post-save close) goes through `user_close`. On
  `EDITOR_ERR_CAPACITY` (the closed pool holds only modified tabs and the
  closing tab is modified, for example a restore placeholder, which closes
  without a prompt) the tab stays open with the notice
  `close refused: every closed tab holds unsaved text; reopen one (Ctrl+Shift+T) first`
  and the editor keeps running. D still drops a prompted modified tab
  outright, so the refusal reaches only prompt-less closes and the post-save
  close.
- **Scroll and minimap.** While the prompt is drawn, the frame's text-scroll
  region and minimap region are disabled, as for the source-change prompt
  (full damage when either was active). Esc's full recompose restores them.
- **Untitled buffers with bytes.** An untitled, non-derived, non-placeholder
  buffer with text (for example from stdin) is treated as unsaved
  (`close_unsaved`). Ctrl+W prompts, and any close marks its tab modified
  first, so the closed record is protected by the eviction rule.
- Backlog, not fixed here: review finding 5 (a peer save leaves a closed
  tab's modified flag stale), a notice when D drops a tab outright, and the
  editor_session_test hang on a failed check with a held save.
