# edit-34l: find in files engine

The engine is independent of editor buffers. `fif_job_init` copies the request
and reserves a `src/base` arena, `fif_submit` schedules bulk filesystem/search
work, and `fif_job_dispose` returns a code if leases or live mail are outstanding.
There is no editor wiring in this bead. Existing find and findui tests stay
unchanged. The requested `build/briefs/_codemap-s7.md` was absent, so the named
headers and implementations were read directly.

`src/file` acquisition/lifecycle belongs to its UI-thread owner; it does not
provide a standalone worker-side read-only buffer. Fif therefore uses
`open(O_NOFOLLOW|O_NONBLOCK)`, `fstat`, and bounded `read` on WORK_BULK. Regular
files are copied into prepaid storage. Files larger than the request's file cap
are skipped with `truncated` set. A growth beyond that cap is detected using an
extra byte. Errors are return codes or DONE fields; unreadable files increment
an error count and the walk continues. The aggregate code records I/O errors.
There is no process-global fif state.

The required `opendir`/`readdir` interface uses libc's allocation for a DIR
handle. That allocation occurs only on worker directory acquisition and is
closed before search/publication resumes. It cannot truthfully be called an
allocation-free directory API. Fif itself never calls malloc, calloc, realloc,
qsort, nftw or fts. Node storage, heapsort, all per-file reading/matching,
context copying, accessors and outbox retry use prepaid memory. Eliminating even
libc's DIR allocation would require changing the requested walker interface.

Messages and cancellation

HIT contains the stored hit index, file index, one-based LF line and byte
column, byte offset and byte length. FILE contains the file index and byte
length. DONE contains stored hits, bytes actually read, searched text-file,
binary, symlink and I/O-error counts, engine/core codes, truncation and
cancellation flags. (P) On the supported ABI their layouts are respectively
40, 16 and 48 bytes: four uint64 fields plus two uint32 fields; one uint64 and
one uint32 with alignment padding; two uint64 fields, four uint32 fields, two
int32 fields, two bools and alignment padding. Static assertions compare each
to WORK_MSG_DATA, the existing 48-byte payload. Message tags derive from the
ASCII FIF namespace and successive enum values.

`work_cancel` invalidates an epoch and suppresses all its messages, including
DONE. (E) A submission therefore atomically reserves two WORK_BULK leases:
one search lease and one companion notification lease. The returned handle is
the search/cancel handle. `fif_completion_handle` exposes the companion for
selective receivers. It waits through cooperative continuations until physical
search completion and publishes DONE independently. A queued cancellation or a
cancelled outbox continuation need not invoke the search function again; the
notifier detects an unfinished search phase and marks the partial summary
cancelled. A normal full mailbox retains exactly one unpublished message in a
prepaid outbox and returns the worker lane via `work_continue`; no spin/sleep
inside the search invocation, and no dropped live results.

Cancelling after the search has physically completed does not retroactively
change its computed summary. As with all work clients, cancellation can discard
already published HIT/FILE mail. Partial counts describe stored/scanned work,
not the number the UI happened to receive. `hits` is the number stored; it is
not an exact total when truncated or cancelled. `files` counts successfully
read nonbinary files; the file-admission cap also counts binary, oversized and
failed selected files. `bytes` includes bytes read for binary probes. A
cancelled companion or pool shutdown cannot promise a DONE: shutdown deliberately
cancels every work lease. Keep the job alive until physical completion and mail
drain in that case as well.

Published result records use release/acquire readiness flags. The UI can read a
record after its message without racing worker writes to later records or
mutable aggregate counters. Paths and contexts never move or change after
publication. Result memory belongs to the job through disposal. The root and
all input bytes are copied during init, so the caller may immediately reuse
its request storage. Unpublished indices are not part of the public contract.

Cap derivations

All capacities are estimates, not latency promises. Zero request limits choose
the named defaults in fif.h. Counts/indices/array sentinels follow their normal
arithmetic meanings; byte-string terminators and the growth sentinel add one
byte (P).

- (E) Depth = FIND_MAX_DEPTH, the existing core nesting cap, 32. This reuses an
  established bounded nesting envelope rather than claiming a measured project
  depth. The iterative DFS has root plus that many directory frames; the public
  limit cannot exceed it. A capped subtree sets `truncated` and is skipped.
- (E) Selected files = FIND_MAX_OFFSETS = 4096 and stored hits = the same existing
  core offset capacity. These reuse an established bounded result envelope;
  neither claims to be a project-size measurement. Limits are caller adjustable.
- (E) Entry capacity = twice selected-file capacity = 8192, allowing one file
  entry plus one directory/other entry per admitted file. It bounds total
  discovered names, including symlinks. Directory identities need root plus at
  most that many entries. Exhausting name storage stops before searching an
  unsorted directory prefix, preserving reproducibility.
- (P) Each discovered path uses the platform PATH_MAX including NUL (4096 bytes
  on this build). (E) Default path storage = 8192 × PATH_MAX = 32 MiB on this
  platform. Paths that do not fit set `truncated`; no partial path is searched.
- (E) Context byte columns = FIND_POLL_UNITS = 1024, reusing a core work quantum.
  Each hit owns cap plus NUL bytes: default 4096 × 1025 = 4,198,400 bytes. It is
  the full original LF-delimited line, with trailing CR removed, truncated from
  the beginning only when longer than the cap. Byte columns are deliberately
  distinct from Unicode display columns. An explicit per-hit flag tells the UI
  whether the line was shortened; it does not make hit counts incomplete.
- (E) File bytes = FIND_MAX_SCRATCH_BYTES × FIND_POLL_UNITS = 256 KiB × 1024 =
  256 MiB, reusing perf's target-A RAM/32 copy threshold. The current file owns
  cap plus sentinel bytes. Insensitive literal search reserves another equal
  array, preserving original context bytes while searching folded bytes. Regex
  searches use the original bytes and the shared case-expansion adapter.
- (E) Binary probe = the requested 8 KiB = FIND_MAX_GROUPS × FIND_POLL_UNITS =
  8 × 1024 bytes. A NUL anywhere in that initial span skips the file. This is
  the requested compatibility heuristic, not a newly measured Sublime claim.
- (E) Read/cancellation span = FIND_POLL_UNITS squared = 1 MiB. Using G6's
  (M)[AC] 16 GB/s, 1,048,576 / 16,000,000,000 = 65.536 microseconds of scan
  traffic, below G6c's existing worker CPU slice. This does not bound blocking
  I/O tail time. Scalar folding/line scanning/context copies and glob DP poll
  every FIND_POLL_UNITS bytes/steps or sooner. The find kernels poll internally.
- (E) Regex expansion retains findui's existing four bytes per query byte plus
  twice the 256-byte alphabet: FIND_OPTIONS_PATTERN_BYTES. Program and scratch
  storage use the existing find size query and FIND_MAX_SCRATCH_BYTES bound.
  Query bytes are bounded by FIND_MAX_PATTERN. Filter text is bounded by
  PATH_MAX and filter rules by FIND_MAX_DEPTH, reusing those established bounds.

Arena sizing sums the checked products of these capacities, sizeof every
record, all file/context/program arrays and worst-case max_align_t padding per
allocation. There are no unaccounted growable containers. Default reserved
virtual memory is dominated by file copies and path storage; MAP_NORESERVE
commits it lazily. Caller limits can reduce it. A failed reservation returns
MEMORY; arithmetic or representation overflow returns ARGUMENT.

Walking and option policy

Each directory is enumerated into its bounded prepaid node slice and sorted
with in-place heapsort before visiting entries in bytewise strcmp order.
Traversal is deterministic depth first. `.git` entries are always skipped.
Every symlink is skipped, so external symlinks and loops are not followed;
(device,inode) tracking also prevents revisiting a directory identity. No nftw
or fts is used. Readdir enumeration never leaves a DIR open across an outbox
continuation.

Where syntax is comma-separated includes and `-` excludes; the separate exclude
string treats every token as an exclusion. Exclusions win and prune directories.
Plain paths select a file or subtree, basename globs such as `*.c` match at any
depth, and slash globs match root-relative paths. Absolute paths lexically
inside an absolute root normalize to relative paths; out-of-root paths do not
match. No implicit second filesystem root, shell expansion, quoted commas or
Sublime project-variable expansion is provided. The matcher supports stars,
double stars across directories, question marks, escaped bytes and byte ranges;
a double star followed by slash also matches no directory. DP has bounded stack
storage and polynomial work, with worker cancellation polls.

Matching delegates to find_literal_next/find_regex_next. ASCII folding and
whole-word boundaries live above the frozen core. The regex folding adapter
was not previously reusable: it lived in findui/worker.c. It is moved into
find/options.c, with findui calling the same implementation. The small fold and
ASCII word predicates are shared by fif through options.h; the existing
snapshot/KMP findui literal implementation remains unchanged. Rejected word
candidates resume at start plus one, preserving possible overlapping accepted
matches. Empty regex matches advance one byte boundary, including EOF once,
following the find API. The engine does not allocate or mutate a piece tree.

What edit-n95 must do

Part two owns the UI controls, Where editing, keys, result panel and activation.
It must keep each job and pool alive through both physical leases and drain all
messages before disposing the job. For a selective receiver, bind both handles;
a generic pool dispatcher can route by generation. On query replacement cancel
the search handle, keep draining, and ignore obsolete generations including
their companion DONE. Use hit indices for fif_results and file indices for
fif_results_path. Convert one-based byte positions to the editor's display
coordinates, show context truncation and incomplete/cancelled/error summaries,
and open files through the editor's normal file owner. Job initialization
prepares bounded arena storage; submission, draining and borrowed accessors do
not allocate on the typing path.

The bench adds only a tracked aspiration: (E) scale existing G6 A p50/p99
80/125 ms per decimal GB by bytes actually read. Directory metadata/open/read
costs are unmodelled, so this is explicitly optimistic and never a gate. The
bench's descriptive sample count (E) is 1/(1−0.99) = 100, giving one nearest-rank
p99 tail observation, not qualified tail confidence. Measurements include
power and load. Fixture literals derive test offsets, line/column expectations
and witness counts; saturation uses WORK_MAILBOX_CAP plus one hit, the smallest
witness exceeding the existing mailbox. No timing deadline is invented.

The queued-cancellation witness parks an earlier bulk lease on an atomic gate,
then submits and cancels the search before releasing the gate. The guarded
mailbox-drain witness waits for dropped_full to advance from a pre-submit
baseline, so saturation in an earlier test cannot satisfy its readiness check.
Both are scheduling-independent handshakes and introduce no timing deadline.
