# edit-fkpk: refresh the mapped source after an own replacement (recovery-3 F5)

## Finding, verified
An original acquired as FILE_MODE_MMAP could be saved once, then never again.
`editor_save_setup` creates one savectl per buffer holding a `file_source`
whose identity (dev, ino, size, mtime, mode, uid, gid, ctime) is captured once
at acquisition. The first save renames the replacement over the pathname;
the mapped inode loses its link and the kernel advances its ctime. The next
save's `file_source_validate` (worker, savectl.c `validate`) compares the full
identity and returns FILE_ERR_CHANGED (5). Red test
`tests/editor_session_test.c:mapped_repeated_save` (copy_threshold 1 forces
MMAP on the 4-byte fixture): the second save fails with error 5 with and
without a journal. The existing `repeated_save` misses it because a 4-byte
file under the default threshold is COPY, and COPY sources validate trivially.

## Fix (the review's smallest fix)
- `file_source_adopt_retired(source, replacement)` in src/file: adopts the
  backing's new ctime only if the fault epoch is unchanged and not faulted,
  the replacement is a different inode than the backing, and the only
  identity difference is ctime (dev, ino, size, mtime, permission bits, uid,
  gid all equal). Anything else leaves the authorization untouched, so the
  next validation still refuses.
- savectl's `replaced` callback (called by `transaction_commit` immediately
  after the rename) calls it for the controller-owned source
  (`options.source`). The pre-rename `validate` just compared the full
  identity including ctime, so the unvalidated window is the rename itself
  plus one fstat.
- ctime is not ignored anywhere else: `file_source_validate` and
  `file_id_diff` are unchanged, so external rewrites are still detected.

## Choices
- Worker-side, inside the save job, not on the UI after SAVED: no UI I/O,
  and the source is exclusively used by the job (validation runs only in the
  controller's single active job; the UI reads the source identity only at
  `savectl_create`). Header states the exclusivity contract.
- Return code of the adoption is ignored in the callback: the save that just
  renamed has succeeded; a refusal leaves the stale identity, which makes the
  next save report "source changed", the conservative state.
- Save As (`commands.c`) passes its source via `validate_source/source_ctx`,
  not `options.source`; its writes rename over a different path, so the
  original inode is untouched and no refresh is needed there.

## Residual
A writer that already holds a descriptor on the original inode and rewrites
bytes while restoring mtime inside the rename window is not detected; the
same tolerance `file_resolve_keep` already applies to retired inodes.
