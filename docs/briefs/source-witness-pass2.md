Send-back (advisor, 2026-11-25): source witness, docs/decisions/wave1-file-source-witness.md (d50a3a3). One more Astra
pass, document only. It runs after Sol's v2-sve (serial Codex).

SIGNED CORE (do not reopen):
- The witness is of the ORIGINAL inode on the held descriptor: not the temp, not the path, not the prefix.
- A ctime move with size and mtime unchanged is settled by content, not by link count or mode. The two-link
  interleaving defeats any stat-only rule.
- Our own rename removes the old backing's read authority, and its first dependent use re-verifies. This closes MF-C
  without a per-save scan.
- A fault or a short read always dominates a CRC match.

W1. The trigger. The witness protects UNSAVED EDITS, so it starts when a mapped buffer first has any: at the first
    committed edit. Keep, a save, or a checkpoint that needs it joins it on demand. Unedited mapped buffers NEVER scan:
    an unedited buffer that sees a foreign change simply reloads.
    - Redo the cost table on that basis.
    - State the remaining exposure exactly: an edit, then a foreign move before the first pass completes, is CONFLICT
      (the first interleaving row).
W2. The states. PENDING vs CAPTURING, and NEEDS_RECHECK vs REVERIFYING, are the same evidence with and without a job
    in flight. That is a transport fact, which belongs in the job slot. COPY is not a state of a mapped source.
    - Proposal: UNWITNESSED, VALID, DISPLACING, RECHECK_OWED, CONFLICT, with the cursor and the job in the job slot.
    - Name the call whose result differs between the members of a merged pair, or merge.
W3. Read tickets, accepted in principle. Every consumer of mapped bytes holds a ticket across its loads. The consumers
    below file's level are piece, undo and the journal's checkpoint producer, whose signed contracts take a
    piece_validate_fn and a context and nothing else.
    - Show the wiring that keeps those signatures unchanged: the ticket lives in the validator's context, owned by the
      L2 consumer that starts the operation (edit for a command, the session for a checkpoint request, file for a save
      stream).
    - If any landed L1 signature has to change, NAME it and the advisor decides. Do not assume it.
W4. The bead (v2-hy8) blocks the WAVE 2 exit (the 10 GiB gate), not Wave 1's. It is implemented after savectl lands.
W5. No direct Save As from IDLE_CHANGED now. Keep followed by Save As already fits.
