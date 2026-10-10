# edit-yqu3: qualified bench populations and fuzz schedules

Scope: edit-yqu slice 3, P4-modules-2 sections 31–39; no production source changes.

The shared interaction minimum remains 10000 (G). Timing percentiles and raw
samples are never multiplied, rescaled, duplicated, averaged between runs, or
filtered after a failure. All verdicts use bench_judge through the shared
reporter. Default MISS/REFUSED remain nonzero; only explicit TRACK opts out.

Repeated full-corpus acquisition is unsuitable for this minimum. Scroll keeps
one owned asynchronous partial-prefix seek as a correctness probe, outside the
qualified population; it then samples repeated indexed resolution and verified
null frames against the same warm corpus. The row name states indexed/null.
The work row uses the shared verdict and retains its maximum/outlier check.
Neither row claims displayed-frame performance or a partial-prefix percentile.

Save uses real durable saves and journal prepare/finish with immutable warm
4 KiB and 64 KiB originals (G workload sizes), rather than a 1 GiB write per
interaction. Four independent controllers, each with its own save and journal
pool and filesystem fixture, supply 2500 observations each (G). The combined
population consists of all 10000 measured latencies (G minimum), including
queueing and I/O delays within each request. Concurrency and workload size are
printed; this does not establish the large-file save gate. A failed run is a
structural failure even in TRACK. Setup and cleanup are outside request timers.

Optional findui count uses fresh warm 64 KiB submappings of the validated all-a
corpus (G workload size), real asynchronous completion, and checks the exact
count and first 4096 offsets (G checked prefix). Its row states the size and
cannot establish full-GiB throughput. CLI counts below the shared minimum,
negative/signed/trailing-junk values, overflow, and excessive sizes are rejected.

Findui regex fuzzing uses an independent recursive syntax tree and endpoint-set
oracle, including concatenation, alternation, groups, nullable branches,
quantifiers, anchors, classes, escapes and malformed/unsupported patterns.
Default folded long literals are generated separately. Existing module #21
still reports FIND_ERR_LIMIT: the fuzzer accepts only that exact known error or
the scalar-correct result; an opt-in expected-behavior regression remains red.

Save fuzzing acquires an actual file-backed MMAP snapshot and retained backing
token, retires the UI file owner, truncates the inode at selected validations,
and reads through the recovery service to create a real sticky backing fault.
Reload rewrites span multiple acquisition chunks and restore mtime after each
chunk. IN_ACCESS starts rewriting after a read, but is not an inter-chunk
barrier: complete old/new snapshots are legal, mixed snapshots are forbidden.
Slot reuse fills the eligible pool after physical completion and mailbox drain,
then interleaves cancellation and logical close. Existing #12 can still return
BUSY for an unrelated epoch; the fuzzer permits that exact known behavior until
unrelated work retires, while an opt-in regression requires immediate success.

Known production regressions run with EDIT_YQU_KNOWN_FAILURES=1. They remain
outside the normal check run because this worker is explicitly forbidden to fix
src/. No failure is presented as fixed. No new global state is introduced.
