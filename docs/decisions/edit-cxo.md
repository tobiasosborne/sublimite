# edit-cxo: a session journal is never visible without its lease

## Problem

`src/main.c` (and the two-process dataloss fixture) created the session
journal with `mkstemp(".../session-XXXXXX")`; the lease
`.session-XXXXXX.lock` was flocked much later, in `editor_session_restore`.
A second instance starting in the same directory runs
`journal_retire_directory` (edit-rfq) during its own startup. In that window
it could take the first instance's lease, find an empty, structurally clean
generated journal, rename it to `.retired` and unlink it. The first instance's
`editor_open` then failed with EDITOR_ERR_IO (-103). If the second instance
only held the lease while scanning, the first instance's `LOCK_NB` flock
failed with the same code. No edits were lost (the placeholder was empty), but
one of two simultaneous launches failed to start.

## Choice: lock first, then create the visible name

`journal_session_create(dir, path, cap, &lease)` (src/journal/session.c):

1. `mkostemps(dir/.session-XXXXXX.lock)`: the lock file is the uniqueness
   carrier. No instance enumerates lock files; they only derive a lock name
   from a visible `session-*` journal.
2. `flock(LOCK_EX|LOCK_NB)` on that descriptor.
3. `open(dir/session-XXXXXX, O_CREAT|O_EXCL)`. Only now is the journal name
   visible to discovery and retirement, and its lease is already held.
4. The held descriptor goes to `editor_config.session_lease`.
   `editor_session_restore` duplicates it (`F_DUPFD_CLOEXEC`) instead of
   opening and flocking a new open file description, which would conflict with
   the creator's own lock. It refuses a descriptor whose inode is not the lock
   file for this journal. The caller closes its copy whenever it likes; main
   keeps it until after its empty-placeholder cleanup.

EEXIST on the journal (a leftover `session-XXXXXX` whose lock file was
removed) or a busy fresh lock (only possible if another instance opened that
name because of such a leftover) keeps the lock file and picks a new name.
Each iteration consumes a new lock name that is never removed, so the
iterations are bounded by the number of such leftovers (P). On any other error
the unused lock is unlinked while still held, so no instance can adopt it.

## Rejected: directory lock across create + lock

Retirement already flocks the directory inode with `LOCK_NB`. Holding that
lock from `mkstemp` until the editor's lease flock would span main.c's backend
selection and display queries, and it would make the creating process's own
`journal_retire_directory` call fail with JOURNAL_BUSY on its own guard
(a new open file description on the same inode). Housekeeping would silently
never run for the instance that holds it. It would also need a lock handed
across the main/editor boundary anyway. Lock-first needs no new lock, keeps
retirement unchanged, and needs no directory-wide serialization.

## Contracts kept

- No malloc on the typing path: all new work is startup only (one mkostemps,
  one flock, one open, one dup, two stats); nothing on input, mutation,
  layout or submit.
- Crash safety (edit-rfq): a crash after step 1 leaves a lock file without a
  journal, which is stable lease infrastructure retirement already ignores.
  A crash after step 3 leaves an empty journal with a lock file, the same
  state as before. Retirement removes it on a later launch under its lease.
- edit-btj retry: the creator's descriptor holds the lease across the
  `recovery_skip` reopen. Previously the lease was briefly released between
  the two attempts.
- edit-rzj discovery is unchanged: it only takes leases of nonempty journals.
- Explicit (non-generated) journal paths and callers that pass no lease keep
  the old open+flock behaviour; single-process tests are unaffected.

## Evidence

Deterministic red/green: `tests/journal_retire_test.c` `startup_window` runs
another instance's `journal_retire_directory` between creation and
`editor_open`. Loop counts are in docs/worker-reports/edit-cxo-s6.md.
