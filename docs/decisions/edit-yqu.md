# edit-yqu — session 9 decisions

## Qualified benchmark samples

Default scroll jumps, savectl small/large saves and findui cancellations retain
exactly the original timed workloads and use BENCH_INTERACTION_MIN_N. Compile
assertions reject smaller interaction sample constants. The three temporary
--track argument files are removed. No gate or sample minimum was weakened to
make a loaded run pass. Full durable-save sampling is expensive; incomplete
runs cannot produce qualified rows. The optional findui count implementation
still needs a separate continuation in its helper header.

## Savectl harness alignment and ordinary instrumentation

Use aligned_alloc(_Alignof(work_pool), sizeof *pool) before work_pool_init.
The type's size is a multiple of its alignment; work_pool_init initializes the
storage. Assert address alignment before initialization in each harness.

The Makefile is frozen and supplies no per-test link flags. The ordinary
savectl test interposes its existing I/O endpoints using direct Linux syscalls,
including fstatat so calls through the file library are guarded too. It compiles
the unmodified production controller translation unit inside the test with a
work_publish macro seam. The static archive consequently does not extract its
savectl object. This preserves the deterministic schedule holding publication
AFTER delivery of the wake, with no manual build variant. The two pre-existing
test guard variables remain the only guard state; no state globals were added.

## Creation permission validity

Append create_mode and create_mode_valid to savectl_options. A valid option
selects exact new-target permission bits, including 0000. The controller forwards
both mode and mode_valid to file_save_write. An invalid option preserves the
legacy baseline.mode hint; zero selects the file core's 0644 default. These
are final fchmod bits, independent of process umask. Existing targets retain
the file core's captured permissions and metadata policy. Setup callers wanting
an umask-derived mode must compute that choice off the typing path and supply
it explicitly. No per-save umask query, allocation, or global state is added.

## Independent fuzz models

Keep scroll's short-line pixel oracle and add a separate long-line/proof/edit
oracle using scalar newline scans and host-owned anchor transformation.
Findui settles only before commands requiring completed results and at oracle
checkpoints; query/source/window edits use bounded partial drains. Its endpoint
set regex oracle covers its supported grammar; arbitrary byte queries stay in
literal mode, rather than declaring unsupported oracle syntax a product error.
Mapped backing is freed by its last lease hook, allowing ASan to detect early
retirement. Replacement fault probes undo their entire successful prefix and
compare against the independently retained original bytes.

Savectl keeps its byte/disk model and adds journal and acquisition sessions.
Journal tests verify retained generations before recovery/retry or explicit
fixture teardown. Fault fixture teardown deliberately abandons unsaved edits
and checkpoints the named disk generation before deleting retained artifacts;
this is test teardown, not production recovery guidance. Timed fuzzing uses
ASan/UBSan with LeakSanitizer disabled in this environment.
