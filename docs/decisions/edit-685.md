# edit-685 — selection indentation rejects LIMIT before mutation

The editor previously deleted a selection through undo, staged the deletion,
and only then queried Enter/brace indentation. A LIMIT result therefore left
a committed deletion prefix and changed the input to the next key attempt.

Use additive pure queries `indent_on_enter_after_delete` and
`indent_on_close_brace_after_delete`. They inspect a logical source consisting
of the original prefix and suffix with the selected half-open range omitted.
Their cursor is the selection's low endpoint; returned brace offsets belong to
the logical post-deletion tree. Existing entry points delegate with an empty
omission, preserving their API and result semantics.

The source maps reads and iterator spans around the omission, without a
snapshot, tree mutation, allocation, or scan through omitted bytes. The existing
per-direction `INDENT_TYPING_BYTES` bound applies to retained logical bytes.
CRLF assembled across the omission obeys the existing split-CRLF rejection.
Output sizing, cleared error results and untouched output on LIMIT/CAPACITY
retain the existing contract. Keep byte consumption inline and put source
mapping in span refill, avoiding a newly introduced per-byte function call.

The editor resolves the indent decision and actual journal insertion capacity
before opening its undo group or deleting any bytes. A rejected query consumes
one input command and increments rejected_commands; it does not retry inside
the command. An immediate repeated key sees the original buffer and selection.
After OK, selection deletion, optional brace dedent, and insertion still use
the existing explicit undo group. Successful mutation errors retain the
existing prefix policy; this bead changes query rejection only.

This supersedes P4.2 integration instructions that compute after actual
selection removal, while retaining their successful-selection semantics.
Querying the unchanged original line alone would wrongly reject selections
that shorten a giant line, or fail to account for deleted line boundaries;
rollback would require undo/journal/index restoration outside this scope.

Tests first reproduced Enter and brace selection deletion on LIMIT, then a
stubbed preview API failed its actual-deletion oracle. Final coverage includes
both scan directions, reversed selections, removed line boundaries, repeated
keys, retained undo and redo, CRLF joins, fragmentation, output exhaustion,
inaccessible mapped omissions, and successful giant-selection atomic replay.
The indent fuzzer compares projected queries with an independently assembled
ordinary tree and checks that the original tree remains unchanged.

Bench evidence and complete verification are in
[the worker report](../worker-reports/edit-685-s9.md). Comparisons were back to
back on the loaded AC machine; they do not certify timing gates. No new globals,
no allocator calls, no undo/piece/index/IPC changes, and no Git writes.

Final gcc 13 `make all` and clang 18 ASan/UBSan `make check` exit zero;
all sixty sanitizer binaries and replay CLI pass (M)[AC]. Editor and indent
fuzz each pass for 61 seconds (M)[AC], against 60 seconds (G). LeakSanitizer was
disabled as required by the sandbox; coordinator leak-enabled checking remains.
The report retains earlier CLI/source-watch validation failures for triage.
