Send-back (advisor, 2026-11-24): the journal runtime section (1b) of docs/decisions/wave1-journal-wire-amendment.md,
from "Proposed runtime section of journal.h" on. NOT signed. One more pass, document only. Then it comes back alone.
edit.h and savectl.h take R1–R6's consequences in the same pass.

KEPT (shape is right):
- The journal does not know undo.
- Begin reserves the final record, and final cannot be refused.
- One request type for every asynchronous effect.

Each item: give the counter-example or make the cut.

R1. journal_command_seal. What it checks is one of two things:
    - a caller bug (claim not consumed, malformed singleton payload): an ASSERT in final under A15;
    - the writer's health, which journal_state already reports.
    Cut it; the caller reads journal_state before its RAM commit. To keep it, name a condition it checks that is
    neither.
R2. journal_command_accepted. Make journal_command_member's result carry it.
    - OK means the member is consumed and the runtime owns the frame from here, written or held.
    - WAIT means it is not consumed and the window is still the caller's.
    The caller's own count is then the truth, and no query exists. While a frame is held the window is not lent, so the
    next member waits.
R3. journal_literal_limit(j). If it cannot differ between two journals in one process, it is a constant in the header
    with its derivation, not a call. Name the product path where it varies, or make it a constant.
R4. journal_source_hold has four callbacks. retain and release exist only to keep the validator's context alive. Undo
    solves the same problem with a lifetime rule (the owner outlives the log) and one validate callback. Do the same:
    - the context outlives the request;
    - close refuses while a request lives;
    - the type is the same validate signature undo uses, declared once where both can include it.
R5. journal_replay_sink is three callbacks invoked from inside the journal on the UI thread, with the session owner on
    the other end calling back into edit and journal. Make it pull, as ruled for undo: after recovery completes, the
    owner takes recovered buffers and controls one at a time, through a call that returns false at the end. No
    callback, no re-entrancy rule.
R6. journal_result exposes journal_receipt with a pinned os_file. Matching epoch, inode and submitted end is the
    journal's own work; a caller needs the publication class and the position. Name the caller that reads the fd, or
    keep the receipt internal. Also, journal_capture's four out-parameters become one out struct.

EDIT.H AND SAVECTL.H (docs/decisions/wave1-edit-spec.md, docs/decisions/wave1-savectl-spec.md): apply R1–R6's
consequences. They call exactly the revised runtime functions. One question to answer: savectl_saved_base and
savectl_save_token return journal types to the host. With finish driven by tick and appended by savectl itself, name
their consumer or cut both.

Also bring forward the signed saved-association rule (wire amendment, at FINISHED; A28 additions): SAVE_FAILED with
CHANGED, the saved id unknown, the buffer dirty, the conflict checkpoint written, no second terminal record. Reflect it
in savectl's states and outcomes.
