# edit-4w1.57 — combined ownership and pending adoption

For P1-1 §5, use six bytes of worker scratch per chunk: uint16 length-minus-one
and uint32 result. A 64-bit scalar carries the continuation offset instead of
retaining an absolute offset array. The result reserves 17 count bits, sufficient
for 65,536 newlines (a completely LF-filled chunk), with independent built and
nonascii flags. Static assertions bind encoding to LINEIDX_CHUNK. Empty-source
jobs decode zero length explicitly. The arrays are immutable geometry and sealed
results under the existing lease/mailbox publication rules. Foreground operations
retain their allocation-free behavior; allocation remains request/open setup.

The existing UI entry remains 16 bytes. This reduces duplicate build metadata
without sharing mutable UI entries with workers. Cancellation still owns scratch
and snapshot bytes until physical retirement. lineidx_mem_bytes reflects the new
arrays, including retiring jobs and declared source bytes.

Successful mmap attachment is the acquisition-prefix handoff. Only after
piece_init_mapped succeeds, file_attach frees the private prefix copy and aliases
the prefix accessor to the full mapping, preserving its length. Borrowed prefix
pointers obtained before attachment expire at handoff; file.h documents this.
Failure keeps acquisition ownership intact. COPY mode retains its existing private
backing and lifetime hooks. No fault/readiness path changes or new globals.

G10f validation uses an ASan allocator census across the production file object,
real piece tree, independent index/find/save snapshots, conservative maximum find
storage and a paused index build. All malloc ownership is counted once, including
opaque records and temporary builder storage (sampled by an instance-local
piece allocator); clean file-backed mapping storage is excluded by the gate.
Cancelled/retiring ownership and snapshots surviving file/tree teardown are also
checked. Peak changes from 9,585,864 to 6,898,888 bytes (M)[AC], under the fixture
allowance of 7,242,880 bytes (G). This test runs in ordinary make check; the release
suite explicitly identifies that the census requires sanitizer instrumentation.

For P1-1 §12, physical completion is insufficient proof of result adoption.
The benchmark waiter keeps pumping bounded slices while mailbox traffic remains,
until index completion or deadline. With no worker/staged work and no pending
messages, an incomplete index can fail immediately. The prior final-publication
recheck remains. A deterministic completed-worker/full-mailbox self-check protects
this seam, and existing missing-start/completion/retirement cases remain bounded.

Red/green transcripts, validation and the consecutive old/compact TRACK comparison
are recorded in `docs/worker-reports/edit-4w1.57-s9.md`. Loaded-box timing variation
does not establish a latency winner; compact scratch is selected for deterministic
ownership reduction. Leak-enabled and cold/real-display gate verdicts remain with
the coordinator.
