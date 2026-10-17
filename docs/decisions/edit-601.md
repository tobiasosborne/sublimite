# edit-601: qct lazy restore recoverability

Bead: edit-601 [BUG] P2. Worker session s8, 2026-10-18. Code: `src/editor/restore.c`,
`src/tabs/tabs.{h,c}` (`tabs_drop`), `src/editor/private.h` (test seam),
comment in `src/editor/editor.c`. Tests: `tests/editor_session_test.c`
`e601_restore_failures` (a)-(d), `tests/tabs_test.c` `drop`.

## Choices

1. **The source is retired only after a checkpoint that returned 0.** If the
   launch has no journal (`e->journal == NULL`), install still swaps the
   restored buffers in. The source is then neither unlinked nor set aside,
   even when a buffer conflicted. Its lock name is kept. The lease is
   released, so the next launch finds the source again. Notice: "large
   session restored; its journal is kept (this launch has no session journal)".
   Replaying it twice is better than losing the only copy.
2. **Retiring the placeholder is the last reversible step's successor.**
   `install_all` keeps `p->retired = false` until the checkpoint succeeds.
   While it swaps and installs, `e->recovering` is set (the startup-recovery
   flag), so `editor_journal_metadata` and the session tree write nothing.
   Before the checkpoint, no TABS or VIEW record can name an id whose BASE is
   not in the journal. After a durable checkpoint, `editor_journal_changed`
   publishes the state that was held back. Save controllers are set up after
   the checkpoint, as `reload_source` does.
3. **Rollback, then the restore_fail path.** If install, close, activation,
   flush or checkpoint fails, `rollback` drops every installed tab and gives
   the placeholder its tab, tree, state and modified flag back. It then
   reselects the tab that was active and reactivates. The restored buffers
   are marked retired, like reload_source's outgoing generation, and
   `editor_retire_buffers` frees them once frames and workers are done.
   Then `restore_fail` runs: the source stays discoverable, a notice is shown,
   and the placeholder becomes an ordinary empty untitled buffer (decision 4
   of edit-qct). `stats.journal_error` is not set for a failed restore
   checkpoint. The rotation failed before replacing the journal, as in
   reload_source's rollback.
4. **`tabs_drop` (new tabs API).** A rollback must remove a tab that was just
   opened without retaining it. `tabs_close` would push it on the closed
   stack and could evict a user's closed tab. `tabs_drop` is `tabs_close`
   with the record freed instead. To drop a closed extra, rollback first
   reopens it (it is the newest closed record), then drops it. The capacity
   check already reserves the transient slot that this needs.
5. **Capacity is checked again at install.** It is the same check as
   `prepare_all` (`fits`). Other tabs may open while the originals attach.
   If capacity is short, the restore is deferred as before.
6. **restore failures never reach fail().** `restore_fail` always returns 0.
   A journal append that fails inside it is still recorded in
   `stats.journal_error`, which is the existing journal policy and not a
   restore error. `editor_restore_service` returns nonzero only when
   reactivation fails during rollback, because the editor itself is then
   inconsistent. The call in editor.c is unchanged apart from a comment that
   states this contract.
7. **Duplicate TABS ids.** The worker dedupes `r->tabs` after the existing
   all-ids-known check, so `live_count`, `prepare_all` and `install_all` see
   distinct ids. `install_all` also bounds its order array at `r->count`.
8. **Test seam.** `editor.restore_fault` follows the per-buffer
   `fail_request` pattern. A nonzero journal code is consumed by the next
   `editor_restore_start` and returned by that scan. The checkpoint failure
   in test (b) needs no seam: the session directory is made 0500, so
   `journal_rotate` cannot create its temp file (JOURNAL_IO), while appends
   through the open descriptor still work. The test skips (b) if the mode is
   not enforced (root).

## Numbers

No new bound, count or duration. Test constants: 1 GiB sparse BASE ((P) 2^30,
the existing FM4_GIB fixture); three duplicate TABS ids ((P) any count above
one overflows the old one-entry order array); 64-id scratch set in the test's
journal checker ((P) the fixtures journal at most two buffers); modes 0500 and
0700 ((P) Unix permission bits).

## Known limits

- With no launch journal, the replacement opens without `origin` (buffers.c
  sets it only when a journal exists or `recovering` is set). The restored
  named buffer is therefore treated as a BASE conflict: disk text, clean.
  Test (a) measured this: restored length 1073741824 = BASE, not BASE + 1
  (M). The source is kept, so nothing is lost. Reported, not fixed (outside the
  four items).
