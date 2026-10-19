# edit-e3z5: Save As retries a refused checkpoint; a dropped Reload says why

This bead follows edit-ivv (docs/decisions/edit-ivv.md) and reuses its retry
rule unchanged. A checkpoint that the shared directory guard refuses with
`JOURNAL_BUSY` keeps its caller's state, and it is asked for again once
`editor_checkpoint_due`, one `JOURNAL_DEFAULT_SYNC_NS` tick later. No new
number is introduced.

## Save As (commands.c `save_as`)

By the time a checkpoint is attempted, the file is already written, so a
refusal from that point on is owed work and never "save as failed":

- **Peer reload refused.** A peer is another live tab open on the target path.
  edit-hw1 admits only an unmodified peer. When the guard refuses the peer's
  reload checkpoint, `reload_source` rolls the reload back. The peer is then
  made clean again (`saved_lost = false`, tab not modified) and stale. That is
  exactly the state `editor_check_sources` already refreshes without a prompt,
  and since edit-ivv it waits for the tick. The peer keeps the BASE staged for
  it (the written file), so every checkpoint in the window still validates. Its
  text is still the old bytes, which the user chose to overwrite.
- **After the first refusal.** No further reload or checkpoint is attempted in
  this command. Another attempt within the same tick would count as a second
  refusal and show the busy notice early. Any remaining peers get the same
  treatment.
- **Command checkpoint refused, or skipped after a refusal.** It becomes
  `busy_checkpoint`, which `editor_checkpoint_owed` pays on the tick, and
  `editor_flush` pays at close or exit. `busy_source` stays empty, so
  `editor_recovery_owed_done` only clears the flag.
- **Any other journal error** is still `EDITOR_ERR_IO`, as before.

## Reload outcomes (buffers.c `reload_source`)

`reload_source` now reports why it left the buffer in place, as an
`editor_reload_outcome`:

| Outcome | Cause |
|---|---|
| `DONE` | the buffer was replaced |
| `REFUSED` | `JOURNAL_BUSY` |
| `DEFERRED` | `file_save_busy` or staged journal ops (`op_count`) |
| `NO_SLOT` | no free buffer slot |
| `FAILED` | acquisition or another checkpoint error |

Each outcome has one behaviour on an explicit Reload:

- **REFUSED and DEFERRED re-arm the choice.** Both clear without the user doing
  anything.
- **NO_SLOT and FAILED show a status-row notice and do not re-arm.**
  - The notice for NO_SLOT is "reload needs a free tab slot: close a tab, then
    press R".
  - The notice for FAILED is "reload failed: the file could not be read
    (R reload / K keep)".
  - Re-arming would leave `source_choice` set, and `runnable()` treats that as
    runnable work. The UI loop would then poll with a zero timeout until the
    user acts, so it would spin.
  - The stale prompt stays, so R works again after the user frees a slot.
    Closing a tab evicts the oldest retained tab, and maintenance frees that
    slot.
  - Either notice is cleared when the next choice is consumed.

The bead says "show a notice and re-arm". This doc splits that per case:
transient causes re-arm, and causes that need the user show a notice. FAILED
is not one of the bead's three named cases. It is included because it was the
same silent drop at the same call site.

The automatic clean refresh is unchanged. It skips only on REFUSED. Every
other outcome falls through to the existing stale-prompt path.

## Numbers

(P) The fixture `tab_capacity = 2` plus `closed_capacity = 1` gives 3 buffer
slots: the stale tab, one retained closed tab and one live tab fill them. Zero
means no refusal and no owed checkpoint. (E) The test watchdogs reuse
`EDITOR_IDLE_NS`, as edit-ivv does.

## Rebase onto edit-ze59 and edit-9fzc (landing, s9)

Since this tree branched, edit-ze59 moved the Save As write onto a worker.
`editor_save_as_tick` adopts the result in `save_as_adopt`, which sets
`publish_owed` for a worker checkpoint (`editor_publish_start`). The UI no
longer runs that checkpoint. edit-9fzc then made the clean peer's reload
(`editor_reload_saved_peer`) mark `publish_owed` instead of checkpointing. As a
result, no checkpoint is left on the UI thread in Save As for the guard to
refuse:

- **Peer reload.** The rule "a refused peer becomes clean and stale" has no
  case to cover any more and was dropped. The peer takes the written bytes at
  adoption, and its checkpoint is the owed publication. A non-DONE peer outcome
  (NO_SLOT, DEFERRED, FAILED) is still "save as failed", as on main.
- **Command checkpoint.** A refused worker publication keeps `publish_owed`
  set (`editor_checkpoint_published`). It is retried on the edit-ivv tick and
  paid by `editor_flush` at exit. Save As therefore never sets
  `busy_checkpoint`, which stays the startup-recovery debt only.
- **Reload outcome.** `editor_reload_source` no longer exists on main. The
  outcome is reported by the static `reload_source` (with 9fzc's
  `defer_checkpoint`), and both `editor_check_sources` callers use it as
  designed above. edit-3620's Keep notice is cleared alongside.
- **Test.** The `e3z5_save_as` test now keeps the bead's acceptance (no "save
  as failed" while the guard is held, then everything lands) under the async
  path:
  - It waits for the adoption and the first refusal (`e->save_as` clear,
    `busy_refusals` set).
  - It checks `publish_owed` where it checked `busy_checkpoint`.
  - It expects the peer to already hold the written bytes, clean.
  - It injects Return without settling, because an owed publication counts as
    pending work until the guard is released.
