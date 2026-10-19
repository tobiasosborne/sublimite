# edit-utin: a peer reload that fails after Save As wrote the file is owed, not "save as failed"

Follows edit-e3z5 (docs/decisions/edit-e3z5.md), which made the JOURNAL_BUSY
peer reload in `save_as_adopt` an owed refresh and left every other non-DONE
outcome as "save as failed". This bead extends that rule to the remaining
outcomes. No new number is introduced.

## Rule (commands.c `save_as_adopt`)

When `save_as_adopt` reaches the peer reloads, the target is already written.
Any outcome other than DONE (`REFUSED`, `DEFERRED`, `NO_SLOT`, `FAILED`) is
therefore handled the e3z5 way:

- The peer is made clean again (`saved_lost = false`, tab not modified) and
  stale (`prompt_ns = 0`). edit-hw1 admitted only an unmodified peer, so its
  text is the old bytes, which the user chose to overwrite.
- The peer is republished to the session. Save As does not report failure, and
  `publish_owed` is set as usual.
- `editor_check_sources` then retries the clean refresh. A transient cause
  (DEFERRED; REFUSED on the edit-ivv tick) clears without the user. A cause
  that persists (NO_SLOT, unreadable FAILED) ends in the existing stale prompt,
  and an explicit Reload there shows the e3z5 notice that says why.
- Only REFUSED stops further peer reloads inside this adoption (the e3z5
  reason: a second refusal within the tick would show the busy notice early).
  After the other outcomes the remaining peers are still tried; NO_SLOT returns
  before any work, and FAILED costs one acquisition attempt per peer.

A non-zero return of `editor_reload_source` itself (the post-replacement
`editor_journal_changed` error) is still `EDITOR_ERR_IO`, as before.

## Reachability, as found

- NO_SLOT: every buffer slot taken (live tabs plus retained closed tabs).
- FAILED: the written target becomes unreadable before adoption, or a non-BUSY
  checkpoint error during the peer reload.
- DEFERRED in this caller: `editor_save_as_tick` adopts only with
  `op_count == 0`, and a reload's checkpoint leaves it 0. `file_save_busy`
  is taken only by the file-level save (`editor_large_save_begin`, called from
  tests only). DEFERRED is therefore not expected here, but it is handled by
  the same rule.

## Tests (tests/dataloss_test.c `--edit-utin`, also in the default run)

- `utin_save_as(UTIN_NO_SLOT)`: (P) slots = tab_capacity + closed_capacity =
  2 + 1, filled by the owner, a retained closed tab and the peer. After Save As
  there is no notice, the owner is rebound and clean, and the peer is clean,
  stale and holds the old text. Closing the owner evicts the retained tab; the
  refresh then lands the written bytes in the peer.
- `utin_save_as(UTIN_UNREADABLE)`: the test runs no editor turn between the
  worker's rename (new inode) and `chmod(target, 0)`, so the adoption finds the
  target unreadable. The editor has no journal, because the journal's BASE
  capture of the written target runs on the same worker right after the write
  and would race the chmod. After chmod 0600 the refresh lands.
- `utin_deferred`: covers the two DEFERRED branches of `editor_reload_source`
  directly. The first is a file-level save held at FILE_STEP_TEMP_WRITTEN
  (`file_set_step_hook`). The second is `e->op_count` set only around the call.
  In both the buffer is left untouched; a reload afterwards is DONE. These are
  coverage tests of existing branches, so they were green before the fix.
- (E) The watchdogs reuse EDITOR_IDLE_NS, as e3z5 and ivv do.

## Rebase onto edit-e3z5 and edit-9fzc (landing, s9)

This tree branched before e3z5 and 9fzc landed. On main, `save_as_adopt`
reloads a clean peer through `editor_reload_saved_peer`, which defers the
peer's checkpoint to the rebind's worker publication (`publish_owed`). The
peer reload therefore can no longer be REFUSED, and e3z5's "refused peer
becomes clean and stale" rule was dropped at its landing. Main still turned
every other outcome (NO_SLOT, DEFERRED, FAILED) into "save as failed"; the
red run of `--edit-utin` against main's `commands.c` reproduced it for
NO_SLOT and the unreadable target.

- **Fix as landed.** `editor_reload_saved_peer` keeps its signature; main's
  own test `!other->retired` already tells DONE from the rest. A peer left in
  place is made clean, stale and republished, exactly as above. The `refused`
  latch is gone, since REFUSED cannot occur here. buffers.c and private.h are
  main's.
- **Tests.** `utin_save_as` is unchanged except that the NO_SLOT case also
  checks the rebind's publication is owed or running after the adoption.
  `utin_deferred` reaches the two DEFERRED branches of the static
  `reload_source` through `editor_reload_saved_peer`, the only exported
  entry. A deferral shows as rc 0 with the buffer neither retired nor
  replaced. The final DONE reload marks `publish_owed`, which the test waits
  out.
