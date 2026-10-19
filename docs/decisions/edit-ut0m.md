# edit-ut0m: refresh the mapped save source after Keep

The regression reproduces external rename-over followed by Keep, another edit,
and save. The buffer is made dirty before rename so normal clean-source reload
does not bypass Keep. Journalling enabled and disabled both refused the save
with FILE_ERR_CHANGED. The fixture forces MMAP with `sizeof ""` as the smallest
nonzero threshold ((P), C string terminator size); its byte lengths derive from
the fixture literals. No large fixture is needed for the regression.

Use `savectl_rebase_kept_target` only in the editor's explicit Keep maintenance,
after source validation and successful checkpoint publication. It refuses
active/recovery ownership before touching the source. For an existing target
whose inode differs from the original, reuse `file_source_adopt_retired` from
[edit-fkpk](edit-fkpk.md): unchanged fault epoch, unfaulted backing, and ctime as
the only changed identity field. Refusal preserves the controller baseline and
the visible source prompt. Ordinary target rebasing stays CPU-only.

The helper's contract now also allows this exclusive, off-path maintenance
call. Keep already stats the backing and captures/checkpoints BASE here; the
new call adds no allocation or typing-path work. Refresh immediately during
Keep rather than deferring until save, so a later backing change is still
rejected by normal complete-identity validation. No validation rule in the
file helper changes; ctime remains significant everywhere else.
