# edit-rfq: startup retirement and manual recovery

Retirement runs at startup while owning the destination lease, before selecting
which remaining session to recover. It is best effort; housekeeping failure
must not prevent recovery. No work is added to input, mutation, layout or submit.

A read-only streaming scan checks record structure, sequence and CRC without
truncating a corrupt tail. Every named BASE must pass the same
`journal_check_base` used by recovery (size, mtime, ctime, inode, device and
prefix CRC). TARGET records must identify that same saved generation for
their buffer; a Save As target cannot be silently discarded. FINISHED SAVE
metadata does not itself make a journal dirty. Untitled and retained-private BASEs are protected. Any INSERT,
DELETE or PREPARED SAVE protects the whole journal, even when edits happen to cancel
out. An unreadable or corrupt journal remains untouched. Live leases protect
both their journals and uncertain retained-original references.

The retirement order is: hold the stable session lease; validate the complete
journal; recheck its inode, size and timestamps; rename with RENAME_NOREPLACE
into a dotted `.retired` name; fsync the directory; verify the renamed inode;
unlink; fsync the directory again. Stable lease files remain in place. A crash
leaves the original or an already-verified retired file. Ordinary discovery
ignores dotted names. Housekeeping revalidates crash-left retired journals and
preserves them if they contain edits. No successful edit can race this protocol
through a cooperating editor because its lease is held throughout.

## Artifact cap and age order

`perf/01-perf-target.md` states no data-directory or on-disk journal budget.
The empty-journal fixture measures a sealed JOURNAL_PAGE and the retained
original measures `strlen("base")`. (M)[AC]: BAT0 `Not charging`, journal
size = JOURNAL_PAGE = 4096 bytes; original size = strlen("base") = 4 bytes.
The format seals the BASE record and its PAD to JOURNAL_PAGE (P: the
record header and PAD header are both JOURNAL_HEADER, and sealed_size rounds
the combined extent up to a page). The fixture count follows the edit-btj profile observation: (M)[AC] 32 sessions.
(E), grounded in those measured sizes: retain eligible artifact bytes up to
32 * (4096 + 4) = 131200 bytes, the observed-count fixture footprint.
This is a storage estimate, not a perf-document budget or a time-to-live.

Age is relative mtime order, with pathname as a deterministic tie breaker:
remove oldest eligible artifacts until the byte cap is met. There is no
invented number of days or generation count. Set-aside journals use the same
complete-empty-log proof; edited/conflicting/corrupt set-asides are pinned.
Retained originals are eligible only when no scanned BASE or SAVE refers to
them and the reference inventory is certain. Pinned bytes count toward the cap
but can exceed it: protecting unsaved edits takes precedence over a hard cap.
Manual replay provides a way to inspect and recover pinned artifacts.

## Manual replay

`build/tools/journal_replay [--apply] [--buffer ID] JOURNAL FILE` prints the
selected buffer's operations and resulting file size. Without `--apply`, it
writes no file. FILE explicitly supplies the starting bytes; a differing BASE
identity is printed, since a set-aside commonly exists precisely because the
original changed. The operator can instead supply a copy of the retained
original. Automatic selection refuses ambiguity across buffer BASEs.

The tool rejects corrupt tails and invalid ranges, checks source and target
identity across processing, writes a private temporary beside FILE, fsyncs it,
then atomically replaces FILE and fsyncs the directory. The source journal is
never rewritten, truncated or deleted. A post-rename directory-sync error is
reported even though replacement may have occurred. Existing Makefile globs
build this tool; no target changes are needed.

Housekeeping also locks the directory inode to serialize inventories. It
creates no directory-wide lock file: existing explicit-journal cleanup tests
proved an extra persistent artifact would break their teardown contract.
Custom-named journals are identified by the existing wire magic (P: ASCII
`JNL1`) and scanned for references. A child directory or symlink makes orphan
proof uncertain, preserving originals that may be referenced below this
inventory. Normal generated-session leases remain stable infrastructure.


The reference inventory is fully gathered before any journal is scanned;
readdir order cannot hide a later retained original. Per-buffer saved identities
use a src/base pool whose capacity is derived from journal file bytes divided
by JOURNAL_HEADER (P: each BASE consumes at least its wire header). TARGET
validation compares the same identity fields as recovery, without assuming
canonical spelling of the target path. Existing explicit-journal cleanup and
headless editor suites validate the directory-inode lock choice.
