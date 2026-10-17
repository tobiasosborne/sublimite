# edit-djg: clean signal exits and recovery notice placement

SIGTERM and SIGINT use a nonblocking `signalfd` in the UI's existing epoll
set. Main blocks only those signals before starting editor workers, which
inherit that mask. The UI consumes the descriptor and requests the same
close as a window close; the existing flush and teardown finish the journal
and release the session lease. Main closes the descriptor after workers have
joined and keeps the termination signals blocked until process exit, so a
repeat signal during teardown cannot interrupt the clean close. There is no
new signal handler or mutable process global; the file module's SIGBUS service
stays untouched.
The descriptor is borrowed through an optional editor configuration pointer,
so library editors do not change their caller's signal policy.

The recovery notice is hidden while find panel rows exist. Opening the panel
composes its complete rows; closing it can show the still-current notice
again. A document mutation continues to retire the notice as before. Save
status priority is unchanged.

Explicit-path recovery remains strict. `recover_existing` means the source
journal is also the editor's live destination, opened through
`journal_open_checkpoint`. Renaming that source aside during installation
would leave the live journal attached to the wrong artifact, requiring a
separate destination and lease transition. Main always creates a fresh leased
destination and uses discovery, so ordinary launches already take the
set-aside path. The explicit-path regression pins `EDITOR_ERR_IO`, a null
editor, unchanged source journal bytes/inode, and unchanged disk text.
`tools/journal_replay` calls `journal_manual_replay`, independently of editor
recovery; its strict source validation is unaffected.

Reapplying edits after an identical-byte rewrite requires a full-file hash
in BASE and remains outside this bead.
