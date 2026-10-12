# edit-2bl: restore the journal kill-safety test

Keep the change in `tests/journal_kill_test.c`. The production journal already
implements both WINDOW schemas and action framing; no writer or editor change
is needed.

The kill campaign's visitor must accept the current WINDOW payload and verify
its chosen-size flag. Retain the legacy payload case through the same visitor.
(P, wire schema from `edit-457.8`): width32 + height32 + flags32 means
`sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t)` = twelve bytes;
legacy width32 + height32 means eight bytes. Inspect the bytes independently
of the production decoder so a writer/decoder mistake cannot satisfy the
campaign's session oracle. Legacy records lack a chosen-size field; the
existing window-policy suite owns the DPI-default behavior.

Retain the seeded ungrouped edit campaign and its independent byte oracle.
Add deterministic SIGKILL cases using the existing append-write seam at the
replacement action's BEGIN, DELETE, pre-COMMIT, and post-COMMIT boundaries.
Flush after DELETE to put PAD and a batch transition inside the group.
Replay must keep the baseline before COMMIT and apply the whole replacement
after COMMIT. Check that markers count as wire records without reaching the
piece-tree visitor. (P): accepted record counts derive from the baseline
INSERT alone, or baseline INSERT + BEGIN + DELETE + INSERT + COMMIT.
The fixture strings determine their own lengths with `strlen`; the boundary
set derives from these wire operations, without a new timing or trial budget.
The campaign's existing dimensions are reused as (E) discriminating fixtures,
not measured display geometry.
