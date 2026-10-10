# P1.7e — file keep/check/save semantics

Scope: `docs/reviews/file-1.md` §§17–23, `src/file`, and its regressions.
Continuation of the inherited session-six/session-seven WIP. The worker leaves
git history unchanged; the coordinator commits and decides bead closure.

- §17: COPY allocates one private original on the prefix worker. The prefix
  aliases its head, BULK fills its suffix, and attachment shares it through
  existing piece lifetime hooks. The allocation stays alive through the last
  snapshot. No full-sized attachment copy or separate retained prefix exists.
  Wrapped allocation accounting covers open, attachment, snapshot, destruction,
  peak storage and final release against G10f (G).
- §18: descriptor and canonical entry identities are validated around prefix
  and full acquisition. Identity includes ctime, permission bits, ownership and
  type. A short read before the captured length is `FILE_ERR_CHANGED`, never
  successful shortened content. Failed acquisition withdraws the prefix and
  full generation and installs sticky changed state through mailbox decode.
  Session eight additionally treats ENOENT, ENOTDIR and ELOOP from `open` after
  successful canonicalization as a changed acquisition. Other errors retain
  their I/O classification and errno. No descriptor/stat work moves onto UI.
- §19: both-absent identities compare equal. Keep can accept deletion,
  truncation and replacement of private originals, and deletion/replacement
  of intact mapped originals, while preserving edits and retained snapshots.
  Missing-path recreation retains the last accepted permission bits. Unexpected
  pathname/backing stat errors refuse keep without changing its baseline.
  Session eight additionally refuses a changed metadata/ctime generation when
  the pathname still names the mapped backing inode. An in-place writer can
  restore mtime without restoring bytes or cached newline counts. This is
  conservative: harmless chmod/chown/ctime changes on that active mapped inode
  can also require reload, since metadata cannot prove the bytes stayed intact.
  The version is captured by the existing backing fstat, without another syscall
  or allocation. Retired mappings still admit intact deletion/rename-over;
  unlink-induced ctime changes cannot be treated as proof of content mutation.
- §20: `file_check` returns sticky state; `reasons` describes the current
  comparison, including zero when a previously detected change remains pending.
- §21: inotify observes parent/name, so rename-over and deletion/recreation
  remain observable. Keep refreshes off path, poll handles ignored/moved/deleted
  watches, and failed setup returns failure even if an inotify fd remains open.
  Poll still reads one bounded buffer and queues asynchronous identity work.
- §22: save captures the current target's metadata through a validated worker
  descriptor. It preserves ownership/group, supported xattrs (including ACLs
  and labels), removes differing inherited attributes, and applies mode after
  ownership/ACL installation, before data fsync. Metadata changes refuse normal
  saves. Preservation errors refuse replacement and remove owned temps; immutable
  attributes already equal on the temp need no successful set. Explicit mode
  zero is distinguished from the default for new targets. An unreadable existing
  mode-zero target safely fails with EACCES instead of widening permissions.
- §23: capture and commit use `fstatat` with `AT_SYMLINK_NOFOLLOW` against the
  pinned save parent. A replacement symlink refuses even FORCE/no-baseline saves.
  Initially opened symlinks still resolve to their canonical target.

## Damaged mapped generations remain incomplete

Keep still refuses modified/faulted mapped backing. This preserves the settled
containment decision in `P1.7c.md` §5. `piece.h` requires unchanged originals
through every tree/snapshot owner and exposes no operation to rebase cached
newline metadata or invalidate immutable snapshots. Detaching or zero-filling
the mapping after truncation cannot recover discarded original bytes or repair
those caches. `FT_CASE=19mapped` retains the explicit failing requested-keep
oracle, outside the ordinary passing suite. Full §19 acceptance is unclaimed.

A future coordinated design must provide stable independent original generations
at acquisition or a reviewed piece/index/undo rebase contract. This worker did
not change those modules or their headers. A forced save does not rehabilitate
the faulted backing or clear its recovery epoch. It therefore does not solve
subsequent ordinary save/keep of a damaged mapping.
Retired mapped backing still lacks a reliable content-version oracle when a
writer also restores mtime: unlink and rewriting can both change ctime. Stable
independent generations are needed for the full immutable-original guarantee.

## Verification policy

The production object retains only the sanctioned mutable `file_bus` service.
Regression-only mutable state is confined to test executables. The strengthened
syscall wrapper covers both `pread` and fortified `__pread_chk`; the injected
read oracle asserts that the injection actually ran. Test-only open interception
reproduces pathname races before any prefix read.

Session-eight evidence is in `docs/worker-reports/edit-4w1.43-s8.md`. Main-source
reproduction uses a separate object under `build/s8-evidence`, without swapping
the worktree source or performing git writes. That reconstructed red run is not
a claim that original WIP session logs survived. No latency measurements or gate
verdicts are claimed; no benchmark variant was selected. Leak-enabled sanitizer
verification remains coordinator work.
