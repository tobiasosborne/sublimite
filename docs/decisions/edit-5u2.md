# edit-5u2: complete checkpoints keep an evicted buffer whose save is in flight

Source: edit-9u3 worker report ("found, not fixed"), bd edit-5u2.

## What the bead claimed, and what actually breaks

The bead says a complete `editor_checkpoint()` taken while an evicted tab's
save is in flight makes the later `journal_save_finish` fail with
JOURNAL_INVALID. That does not reproduce on main (f477c02): since edit-9u3 the
saving buffer keeps its session leaf, and the finish checkpoint is encoded from
the session graph (`prepare()` in src/editor/session.c), not from
`editor_checkpoint()`. The test `checkpoint_during_evicted_save` (hold the
write, evict, `journal_flush` + `editor_checkpoint`, release) passes before and
after this change; it stays as a regression guard.

The real defect is crash safety in the window between that checkpoint and the
finish. `editor_checkpoint()` skipped every retired buffer, so the rotated
CURRENT no longer named the evicted saving buffer at all. A crash before the
rename then lost the Ctrl+S text: the disk still holds the old bytes and the
journal no longer holds the new ones. Without the checkpoint the same crash
recovers the text as a closed tab (the PREPARE checkpoint carries it). The red
test `crash_after_evicted_checkpoint` shows exactly that difference.

## Choice

`editor_checkpoint()` includes a retired buffer iff it is the session's saving
buffer (`editor_session_retains`), encoded as a closed tab (absent from TABS,
consistent with edit-9u3's metadata), with:

- an empty BASE, its TARGET, and its full text as INSERTs. This is the shape
  CURRENT cancellation already uses (`c->replacement.path = ""`). The target can
  be replaced by the in-flight write at any moment and the retained generation
  belongs to the save token, so neither original nor retained identity is a
  safe BASE for a checkpoint taken outside the save transaction. Recovery
  compares the recovered text with the disk (`same_bytes`), so the closed tab
  comes back modified if the rename did not happen and clean if it did.
- the view from its retained session leaf (the last published tab state).

Live buffers are encoded as before. A live saving buffer's own BASE behaviour
is unchanged (out of scope).

The retained-generation artifact is no longer referenced by that CURRENT; if
the process crashes in the window, the startup retirement sweep treats it as an
unreferenced base artifact (src/journal/retire.c, `eligible`). If it does not
crash, the session-graph finish/cancel checkpoint and token own it as before.

## Retire guard

`editor_retire_buffers()` now refuses explicitly to forget/destroy the saving
buffer (`editor_session_retains(e, b, NULL)`). No red test is possible for it
today: retire returns early while the save is active, and a blocked save
implies `can_recover_cancel` (blocked = can_recover_cancel || needs_finish, and
needs_finish implies a prepared token, which with a journal implies
can_recover_cancel), so the existing checks already keep it. The guard removes
the dependence on that chain.
