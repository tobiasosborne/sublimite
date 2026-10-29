# edit-m25u — descriptors per open file, the soft fd limit, EMFILE refusals

## 1. The second per-file descriptor

**What it was (M, 2026-10-29, this worktree, `ls -l /proc/PID/fd` on an
instance on :98).** Five client opens added ten descriptors: each file
(`lr-x ... -> .../files/fN.txt`, the retained source the file module maps or
copies from) and one `lr-x ... -> /tmp`. `/proc/PID/fdinfo` for the second
showed `flags 02300000` (O_RDONLY | O_DIRECTORY | O_LARGEFILE | O_CLOEXEC),
same inode as `/tmp`. That is `os_dir_open` on `os_temp_dir()`.

**Owner.** `savectl_create` (`src/savectl/savectl.c`). Every save controller,
which means one per open tab, plus the Save As writer, opened its own
`os_dir reload_cache` on `$TMPDIR` or `/tmp`. The editor never passes
`reload_cache_dir`, so every controller pinned the same directory.

**Why it exists.** edit-ck4 ("Reload private backing", after review P4-3
finding 33): a reload copies the changed file into private backing. It tries
an O_TMPFILE in the source's parent first. If that fails (no write permission,
or no O_TMPFILE support), it falls back to a cache directory *descriptor
retained at controller setup*. The fallback is an anonymous file, or a
random exclusive no-follow name that is created and unlinked through that same
descriptor. edit-1kqw kept the pin (`os_file_open_anon` on a pinned `os_dir`).
The invariant is that a reload never resolves the cache path again after setup,
and create and cleanup go through one directory object.

**Can it be shared?** Yes, without weakening anything. The descriptor is only
read: `os_file_open_anon`, `os_file_open_at` and `os_unlink_at` all take a
`const os_dir *`, so concurrent use by workers of different controllers is
just concurrent `openat`/`unlinkat` on one dirfd. The O_PATH fallback reopen in
os_posix.c (`os_dir_sync`/lock) is never used on this directory. Pinning at
setup is kept: the directory is resolved once when the editor opens, not once
per tab, and never again. Save temporaries are not involved; they belong to
the file module and live in the target's directory. Watches are not involved
either: `file_watches` already shares parent watches behind one inotify fd.

**Change (small, local).** `savectl_cache`, a reference-counted pinned
directory (`savectl_cache_open`, `savectl_cache_release`), plus an optional
`savectl_options.reload_cache`. A controller retains the cache and releases it
in `savectl_destroy`, so lifetime is independent of teardown order. Without
the option, a controller opens its own, as before, so tests and other callers
are unchanged. The editor opens one cache in `open_editor`, passes it to all
three `savectl_create` sites (session save setup, Save As writer, Save As
adopt) and releases its reference in `editor_close`. If that open fails, the
pointer stays NULL and each controller pins its own, as before. Result (M): 31
opens held 31 descriptors, not 62 (`ipc_test fd_per_file`). A 600-file
instance held 631, and exactly one of them is on `/tmp`.

## 2. Soft RLIMIT_NOFILE

`os_fd_limit_raise(uint64_t *soft)`, declared in os.h for POSIX only (the same
pattern as `os_dir_fd`), called once at the top of `src/main.c`. The result is
ignored, with no output. It is off the typing path: one getrlimit, one small
read and one setrlimit before any thread exists. It raises the soft limit to
the hard limit and never lowers it.

**Bound.** If the hard limit is RLIM_INFINITY or above `/proc/sys/fs/nr_open`,
the target is nr_open, because the kernel refuses a larger value with EPERM.
If that file cannot be read, the target is 1048576 = 1024 × 1024 (P: the
default `sysctl_nr_open`, fs/file.c). No other cap is added. The kernel grows
the descriptor table on demand, so a high limit costs nothing until it is used.
Nothing in src/ or vendor/ calls select()/FD_SET (which break above
FD_SETSIZE 1024) or loops up to _SC_OPEN_MAX (grep, 2026-10-29).
`os_proc_spawn` closes inherited descriptors with closefrom, not a loop.
Windows needs nothing: it has no per-process soft handle cap of this kind.

## 3. Refusals for lack of descriptors

**Today (M, before the change, soft 64, sequential client opens).** Opens 1-16
added two fds each. Open 17 was accepted with *one*: the file opened, but its
controller's `os_dir_open` hit EMFILE. `editor_save_setup`'s error is recorded
in `b->save_error` and the tab installs anyway, so the tab could not save.
That is half state. Opens 18 and later were refused whole (the file worker's
open failed, the buffer was destroyed, no tab), and each client printed
"running instance rejected the open request". The editor showed nothing, so a
desktop launch, whose client stderr goes nowhere, failed silently.

**After.** Sharing the cache removes the descriptor from controller setup, so
the boundary open no longer half-installs. `editor_buffer_prepare` records
EMFILE/ENFILE (from the file's worker errno or synchronous errno) on the
editor. `editor_open_request`, after its whole-request rollback, sets the
status-row notice "open refused: too many open files" once. Further refusals
find the same pointer and do nothing, so a burst shows one notice. The notice
ends at the next mutation, like every recovery notice. The IPC client still
receives IPC_REJECTED (unchanged).
