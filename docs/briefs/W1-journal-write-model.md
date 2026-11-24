W1 — the journal write model (advisor, 2026-11-24, joint read of savectl 62c1441 and edit aa25e91). For Astra: prove or
break it, by document and source reading only. Output: docs/decisions/wave1-journal-write-model.md.

WHY:
- Astra's E2 answer is right in what it refutes, and the advisor accepts both refutations:
  - tail carry fails machine loss (an old inode's sync cannot be transferred);
  - passing the writer per record fails under the present contiguity rule.
- The advisor does not accept the conclusion. A private complete image with an optimistic compare-and-switch for
  checkpoint, Keep, finish and cancel:
  - has no completion bound while the user types (the Wave 1 exit says "save while typing continues");
  - writes a whole session image to protect one peer;
  - opens four of the eight GAPs.
- The edit spec has the same disease elsewhere: a long command batch may not be interleaved, so a large undo or paste in
  one buffer stops every other buffer's journal writes for a time proportional to its length.
- Both follow from one wire rule: a group is contiguous. The advisor reopens that rule. The wire is frozen at the Wave 1
  exit, not before.

THE PROPOSAL:
a. ONE WRITER.
   - Only the UI thread writes the named journal: one complete record per os_file_pwrite_once, between input events, at
     least one ready record per loop turn.
   - Workers never write it. They read pinned snapshots, encode and CRC records into bounded buffers handed over by
     mailbox, and sync the named inode through a given offset (a sync needs no admission).
   - No grant, lease or exclusive section exists anywhere.
b. GROUPS ARE PER BUFFER AND NEED NOT BE CONTIGUOUS.
   - Every member carries its buffer id and its group kind (command or checkpoint). Records of other buffers may lie
     between members.
   - The reader works in two passes and applies a group at its final record.
   - An unterminated group, or a command group superseded by a later command start for the same buffer, is ignored
     alone, and nothing else is.
c. A CHECKPOINT GROUP'S BEGIN FIXES ITS BOUNDARY.
   - The snapshot is pinned at that record, and a worker encodes it as members.
   - The same buffer's later ordinary commands may lie between the members and are applied after the checkpoint text.
d. THE RAM COMMIT IS BRACKETED.
   - For a multi-record command, every record except the last reaches the journal before undo_commit. The last one
     (GROUP_COMMIT, or the single record of a one-record command) follows it.
   - So every source read and its validation precedes the commit. A failed read or write before the commit is
     undo_abort and an ignored group, not a committed edit that cannot be journaled.
   - After the commit exactly one record of bounded size remains.
   - The signed undo erratum already keeps the prepared checkpoint open across turns.
e. Finish, cancel, Keep, conflict and peer protection are appended records and groups, plus worker syncs through an
   offset. No image is built for any of them.
f. ROTATION IS THE ONLY IMAGE SWITCH. It is Astra's exact-match compare-and-switch, attempted only while no save token
   and no group is open. Completion at quiescence suffices because nothing waits for a rotation.

Invariant claimed: every acknowledged command is a complete record sequence in the one named inode, and a key waits
for nothing but the record its own turn is writing.

DELIVER:
(1) A proof, or the breaking cut: the interleaving and the crash point that loses an acknowledged command, applies a
    partial group, or breaks T2/T3 peer protection or T5. Check against K and M at every cut of T1–T5 and the edit kill
    cuts. In particular try:
    - a same-buffer command between checkpoint members;
    - a kill between BEGIN and the final record, with acknowledged keys after BEGIN;
    - machine loss with the sync through the final record and later records lost;
    - two open checkpoint groups;
    - a command group of B among checkpoint members of A;
    - an aborted group followed by a new command.
(2) The wire delta against landed 2.1: which header fields exist, what the reader must hold in memory, and its cost.
(3) Which of the eight GAPs disappear and which remain.
(4) What in L2-2b's wip (t3/journal-runtime, a94af15) is invalidated.
(5) The simplest alternatives and what defeats each:
    - the exclusive grant: a key waits in proportion to another buffer's length;
    - the private-image switch: no completion bound, and a session image per peer.

If W1 stands, edit.h and savectl.h are redrawn on it (no lease observable, no post-commit streaming state, no grant
table) and go to the advisor once more. If it falls, the breaking cut decides what replaces it. Nothing lands on the
wire until the advisor signs.
