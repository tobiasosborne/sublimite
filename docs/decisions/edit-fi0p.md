# edit-fi0p: recovery backlog

F4: explicit off-path `editor_flush` first joins journal transport, then calls
`journal_retry` on sticky IO before its existing checkpoint fallback. This
repairs a post-rename parent barrier before rotation admission compares
accepted and durable sequences. A failed writeback still refuses retry and
requires a complete checkpoint into a fresh inode. Staged editor state keeps
its existing checkpoint obligation. No typing-path work or allocation added.

The regression injects a failed parent barrier after successful rename,
checks that an unsuccessful retry does not acknowledge durability, restores
storage, and flushes through the editor API. (P) Fixture byte lengths come
from literals; structural zero/one values mean empty/success, first index,
Boolean states and unit increments. No new timing or storage budget.

F7: materialisation validates the opened descriptor against the recorded
BASE before reading, then checks descriptor and pathname identity after the
full read. The captured prefix CRC comes from those actual materialised
bytes. A mismatch returns `JOURNAL_BASE_CHANGED`; discovered-source recovery
can use its existing tolerant conflict/set-aside path. Whole-text fallback
remains valid only because this read established an immutable copy of the
recorded generation, independently of the generation subsequently acquired
for TARGET. Tests intercept recovery's bulk read with an included translation
unit; they rewrite immediately before and during it without changing journal
validation or adding a production hook.

F8: Save As applies the existing saved-history refusal predicate to retained
closed peers as well as live peers, before creating its worker writer. The
closed peer keeps its old BASE and edits. The regression disables retention
on literal-sized named files to exercise the same unprotected BASE state as
an over-bound file without an unnecessary large fixture. Broader Save As
transaction guarantees remain unchanged.

F9: record the buffer revision when an asynchronous copy becomes FAILED.
A later revision change or BASE identity change permits IDLE and submission
on a following maintenance tick. A stationary failed buffer never retries
every tick; the worker still validates the original generation. The test
fails the copy's metadata barrier, verifies idle ticks do not retry, mutates,
waits for durable reference publication, rewrites the original, and recovers
from the retained copy. (P) The attempt count is the failed copy plus its
mutation-authorised successful retry; no timer or retry budget is added.

A repaired flush also completes a CURRENT checkpoint, so editor bookkeeping
for staged edits and retained BASE references is adopted even if the failed
rotation had already renamed its inode before returning IO.

F11: the lazy scanner saves TABS' active source ID before deduplicating its
ID array. Installation maps that source ID through the ordered installed
buffers and selects it before checkpoint publication. Missing/invalid TABS
keeps the existing first-buffer fallback. Regressions cover the second active
tab with unique IDs and with a repeated earlier ID, then hot-exit/relaunch.
(P) The fixture contains the first and second literal buffers; duplicate
TABS places the first ID twice before the second, so deduplication changes
the second buffer's numerical index without changing its identity.

F12: `build/tools/journal_replay --acknowledge RESERVED_SET_ASIDE` is a
separate operator action after all buffers in that source have been manually
resolved. Replaying a selected buffer never implicitly acknowledges the
whole source. The operation accepts only a parsed reserved rotation set-aside,
takes nonblocking exclusive directory and file leases, verifies a complete
CRC-valid source through its held parent directory, and rechecks descriptor
and pathname identity before unlink plus parent fsync. Ordinary journals,
symlinks, torn/corrupt sources, and busy/changing sources are preserved.
A post-unlink directory-sync failure returns IO; the acknowledged source may
already be absent, or reappear after power loss. The command never adds an
automatic deletion policy. Remaining unreferenced BASEs then use the existing
inventory and cap. The regression manually resolves a source, checks busy and
ordinary-name refusals, acknowledges it through CLI and sanitizer-linked API,
and verifies that an over-cap orphan is reclaimed and recovered text stays.
(P) The sparse orphan exceeds the existing artifact cap by the smallest
positive byte increment; no new budget, count or age threshold is introduced.
