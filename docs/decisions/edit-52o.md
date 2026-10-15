# edit-52o: retire dead rotation temporaries before journal inventory

Rotation writes a fresh inode, syncs it, and atomically replaces the journal
with rename. It never unlinks the original before rename. Nevertheless an
original can be missing from the directory, so housekeeping must preserve a
possible sole copy. Content is not evidence for deleting a temporary: a partial
checkpoint can contain INSERTs, BASE references, or an invalid tail.

Future temp names use the reserved prefix `.sublimite-journal-from-`, followed
by original device, inode, ctime seconds, ctime nanoseconds, PID, monotonic
nonce, collision attempt, and the original basename encoded as hex bytes.
All hexadecimal is lowercase. The original's
identity comes from fstat of the open original journal immediately before
creation. Ctime supplements device/inode against reuse after the original
disappears, using the same generation evidence as BASE identities. The exact
original basename must still name that generation; a temp or a fresh launch's
different session name cannot prove its own redundancy after inode reuse
within a filesystem timestamp tick. A missing or
changed generation means rename the temp once to its existing
`<name>.set-aside-<realtime seconds><nanoseconds>` convention, with
RENAME_NOREPLACE. No journal content is read for either outcome.

Historical `.sublimite-journal-<PID>-<nonce>-<attempt>` names contain no original
identity. Directory state cannot associate them with a particular session,
even when unrelated session journals exist. Treat these ambiguous originals
as missing: set the temp aside once instead of guessing that it is redundant.
Very long original basenames that cannot fit the original identity, encoded
basename and a future set-aside suffix also use the historical spelling.
Successful rotation retains the existing full-basename/path support. This
preserves the historical profile's ambiguous bytes; their eventual
manual deletion requires independent confirmation that the original survives.
This is the coordinator's possible-sole-copy exception, not a content scan or
a new retention budget.

`journal_rotate` now holds a shared flock on a separately opened directory
inode before creating any temp, through publication, directory sync and temp
cleanup. Previously only editor callers' outer session leases existed; the
journal API itself had no covering lease, nor a way for retirement to associate
the temp with that lease. Retirement already takes an exclusive, nonblocking
flock on this directory inode. Reusing that protocol closes the creation
window without lock files, process probes, timeouts, or allocation on the
typing path. Concurrent rotations can share the directory lease; housekeeping
returns JOURNAL_BUSY while any rotation is alive. Rotation is an existing
off-path, blocking operation and waits if an inventory already owns the inode.

For historical live writers, any busy existing owner lock other than this
launch's fresh destination conservatively protects all legacy temps. The
liveness check opens existing lock files only. An independently held flock on
the temp inode also protects it. All checks reuse LOCK_EX with LOCK_NB.

The sweep runs before ordinary retirement can remove an original. It checks
regular-file identity before mutation and fsyncs the directory after changes.
It never calls retirement's `lease()` for a temp. A separate pass opens and
locks existing `..sublimite-journal-*.lock` litter, checks the locked inode is
still named, and unlinks it after its temp is gone. This also cleans orphan
temp locks whose temp disappeared before this launch.

The whole reserved temp namespace, including set-asides and malformed names,
is excluded from magic probing, journal scanning, lease creation, age ordering
and the artifact cap. Set-asides stay unchanged on later launches. Remaining
temps make the BASE-reference inventory uncertain: their potentially unique
BASE references are protected without reading them. Normal session journals,
including the BASE-only clean tab sets from edit-28u, keep their existing rules.

Number derivations: (P) hexadecimal radix is sixteen; decimal timespec
nanoseconds occupy nine digits, matching existing set-aside naming.
Each original-basename byte requires two hex digits. NAME_MAX plus the NUL
bounds the temp basename; the reserved set-aside suffix derives from its
literal, time_t's bit count as a safe upper bound on decimal seconds including
sign, and the decimal nanosecond field. Existing path schema capacity plus
that basename bounds the rename-hook transport path. Fixture transport is a
JOURNAL_PAGE checkpoint, derived from the existing sealed journal page, with
an offset uint64_t plus the literal `checkpoint` as the INSERT payload.
Other fixture integers denote empty/single, pipe read/write endpoints, or
protocol alternatives. No new size budget, duration, expiry age, or retry
count was introduced; the encoder's inherited collision ceiling is unchanged.

Fuzz coverage exercises surviving, missing and changed original identities,
legacy names, malformed grammar and overflow, pinned set-asides, and a live
temp inode lease. Input bytes supply the temp content; directory identity
and the chosen operation supply the oracle. The parser never supplies its own
expected outcome.
